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


bool Contains(ClassesSet &set, const CXXRecordDecl *elt) {
  return set.find(elt->getCanonicalDecl()) != set.end();
}

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

      if (!m->isDefined())
        undefinedClasses->insert(r);

      if (m->getAccess() == AS_private)
        privateMethods->insert(m);
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

      WarnUnused(ctx, undefinedClasses, unusedPrivateMethods);
    }
  private:
    void WarnUnused(ASTContext &ctx, ClassesSet &undefined,
        MethodSet &unused) {
      DiagnosticsEngine &diags = ctx.getDiagnostics();

      for (MethodSet::iterator I = unused.begin(), E = unused.end();
          I != E; ++I) {
        const CXXMethodDecl *m = *I;

        /* care only about fully defined classes */
        if (!IsFullyDefined(undefined, m->getParent()))
          continue;

        /* some people declare private never used ctors/dtors purposefully */
        if (dyn_cast<CXXConstructorDecl>(m) || dyn_cast<CXXDestructorDecl>(m))
          continue;
        
        PrintUnusedWarning(diags, m);
      }
    }

    // if all the class methods and its friend functions/friends' methods
    // are defined
    bool IsFullyDefined(ClassesSet &undefined, const CXXRecordDecl *r) {
      if (Contains(undefined, r))
        return false;
      for (CXXRecordDecl::friend_iterator I = r->friend_begin(),
          E = r->friend_end(); I != E; ++I) {
        const NamedDecl *fDecl = (*I)->getFriendDecl();
        const FunctionDecl *fFun = dyn_cast_or_null<FunctionDecl>(fDecl);
        const CXXRecordDecl *fRec = dyn_cast_or_null<CXXRecordDecl>(fDecl);

        if (fFun) {
          if (!fFun->getCanonicalDecl()->isDefined())
            return false;
        } else if (fRec) {
          if (Contains(undefined, fRec))
            return false;
        }
      }
      return true;
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
