#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/AST.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

namespace {

typedef llvm::DenseSet<const CXXMethodDecl *> MethodSet;
typedef llvm::DenseSet<const CXXRecordDecl *> ClassesSet;


class DeclRemover : public RecursiveASTVisitor<DeclRemover> {

};

class DeclCollector : public RecursiveASTVisitor<DeclCollector> {
  public:
    DeclCollector(ClassesSet *undefined, MethodSet *privateOnes)
      : undefinedClasses(undefined), privateMethods(privateOnes) { }

    bool VisitDecl(Decl *d) {
      const CXXMethodDecl *m = dyn_cast<CXXMethodDecl>(d);
      const CXXRecordDecl *r;
      if (!m || !(m = m->getCanonicalDecl()) || !(r = m->getParent())
          || !(r = r->getCanonicalDecl()))
        return true;

      if (m->getAccess() == AS_private)
        privateMethods->insert(m);
      if (!m->isDefined())
        undefinedClasses->insert(r);
      return true;
    }
  private:
    ClassesSet *undefinedClasses;
    MethodSet *privateMethods;
};

class DeadConsumer : public ASTConsumer {
  public:
    virtual void HandleTranslationUnit(ASTContext &ctx) {
      MethodSet unusedPrivateMethods;
      ClassesSet undefinedClasses;
      /* gather lists of: all methods and the private ones only */
      DeclCollector collector(&undefinedClasses, &unusedPrivateMethods);
      collector.TraverseDecl(ctx.getTranslationUnitDecl());
      llvm::errs() << "found " << undefinedClasses.size() << " undefined classes\n";
      llvm::errs() << "found " << unusedPrivateMethods.size()
        << " private methods\n";
    }
};

class DeadAction : public PluginASTAction {
  protected:
    ASTConsumer *CreateASTConsumer(CompilerInstance &, StringRef) {
      return new DeadConsumer();
    }
    bool ParseArgs(const CompilerInstance &,
        const std::vector<std::string>& args) {
      if (args.size() && args[0] == "help")
        llvm::errs() << "DeadMethod plugin: warn if fully defined classes "
          "with unused private methods found\n";
      return true;
    }
};

}

static FrontendPluginRegistry::Add<DeadAction>
X("dead-method", "look for unused private methods");
