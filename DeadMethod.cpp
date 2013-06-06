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
  public:
    DeclRemover(MethodSet *privateOnes) : unusedMethods(privateOnes) { }

    bool VisitCallExpr(CallExpr *call) {
      const CXXMemberCallExpr *memberCall = dyn_cast<CXXMemberCallExpr>(call);
      if (memberCall)
        FlagMethodUsed(memberCall->getMethodDecl());

      return true;
    }

    bool VisitDeclRefExpr(DeclRefExpr *expr) {
      FlagMethodUsed(dyn_cast<CXXMethodDecl>(expr->getDecl()));
      return true;
    }
  private:
    MethodSet *unusedMethods;

    // removes the method from the unused methods set; deals with NULL
    // pointers as well
    void FlagMethodUsed(CXXMethodDecl *m) {
      if (!m || !(m = m->getCanonicalDecl()))
        return;

      unusedMethods->erase(m);
    }
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
      TranslationUnitDecl *tuDecl = ctx.getTranslationUnitDecl();

      /* gather lists of: not fully defined classes and all the private
       * methods */
      DeclCollector collector(&undefinedClasses, &unusedPrivateMethods);
      collector.TraverseDecl(tuDecl);
      llvm::errs() << "undefined classes: " << undefinedClasses.size() << "\n";
      llvm::errs() << "private methods: " << unusedPrivateMethods.size()
        << "\n";

      DeclRemover remover(&unusedPrivateMethods);
      remover.TraverseDecl(tuDecl);

      WarnUnused(ctx, &undefinedClasses, &unusedPrivateMethods);
    }
  private:
    void WarnUnused(ASTContext &ctx, ClassesSet *undefined,
        MethodSet *unused) {
      DiagnosticsEngine &diags = ctx.getDiagnostics();

      for (MethodSet::iterator I = unused->begin(), E = unused->end();
          I != E; ++I) {
        const CXXMethodDecl *m = *I;
        const CXXRecordDecl *r = m->getParent()->getCanonicalDecl();

        /* care only about fully defined classes */
        if (undefined->find(r) != undefined->end())
          continue;

        /* some people declare private never used ctors purposefully */
        if (dyn_cast<CXXConstructorDecl>(m))
          continue;

        /* the same with dtors */
        if (dyn_cast<CXXDestructorDecl>(m))
          continue;

        PrintUnusedWarning(diags, m);
      }
    }

    void PrintUnusedWarning(DiagnosticsEngine &diags, const CXXMethodDecl *m) {
      unsigned diagId = diags.getCustomDiagID(DiagnosticsEngine::Warning,
          "private method %0 seems to be unused");
      diags.Report(m->getLocation(), diagId) << m->getQualifiedNameAsString();
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
