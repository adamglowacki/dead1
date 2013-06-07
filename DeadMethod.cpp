// Clang plugin: dead-method
// Author: Adam Głowacki
//
// Detects unused private methods in classes and prints warnings. Omits
// classes that are not fully defined in the current translation unit and
// these whose friends are not defined here.
//
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/AST.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

namespace {

typedef llvm::DenseSet<const CXXMethodDecl *> MethodSet;
typedef llvm::DenseSet<const Type *> ClassSet;

// set manipulation functions
bool Contains(ASTContext &ctx, ClassSet &set, const QualType elt) {
  const Type *t = ctx.getCanonicalType(elt).getTypePtrOrNull();
  if (!t)
    return false;
  return set.find(t) != set.end();
}

void Insert(ASTContext &ctx, ClassSet &set, const QualType elt) {
  const Type *t = ctx.getCanonicalType(elt).getTypePtrOrNull();
  if (t)
    set.insert(t);
}

// mark off the used methods
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

    // remove the method from the unused methods set; deals with NULL
    // pointers as well
    void FlagMethodUsed(CXXMethodDecl *m) {
      if (!m || !(m = m->getCanonicalDecl()))
        return;

      unusedMethods->erase(m);
    }
};

// gather:
//  - classes with undefined methods
//  - declared private methods
class DeclCollector : public RecursiveASTVisitor<DeclCollector> {
  public:
    DeclCollector(ASTContext *c, ClassSet *u, MethodSet *p)
      : ctx(c), undefinedClasses(u), privateMethods(p) { }

    bool VisitDecl(Decl *d) {
      const CXXMethodDecl *m = dyn_cast<CXXMethodDecl>(d);
      const CXXRecordDecl *r;
      if (!m || !(m = m->getCanonicalDecl()) || !(r = m->getParent())
          || !(r = r->getCanonicalDecl()))
        return true;

      if (!m->isDefined())
        Insert(*ctx, *undefinedClasses, ctx->getRecordType(r));

      if (m->getAccess() == AS_private)
        privateMethods->insert(m);
      return true;
    }
  private:
    ASTContext *ctx;
    ClassSet *undefinedClasses;
    MethodSet *privateMethods;
};

// deal with every translation unit separately
class DeadConsumer : public ASTConsumer {
  public:
    virtual void HandleTranslationUnit(ASTContext &ctx) {
      MethodSet unusedPrivateMethods;
      ClassSet undefinedClasses;
      TranslationUnitDecl *tuDecl = ctx.getTranslationUnitDecl();

      // gather lists of:
      //  - not fully defined classes
      //  - all the private methods
      DeclCollector collector(&ctx, &undefinedClasses, &unusedPrivateMethods);
      collector.TraverseDecl(tuDecl);
      llvm::errs() << "undefined classes: " << undefinedClasses.size() << "\n";
      llvm::errs() << "private methods: " << unusedPrivateMethods.size()
        << "\n";

      DeclRemover remover(&unusedPrivateMethods);
      remover.TraverseDecl(tuDecl);

      WarnUnused(ctx, undefinedClasses, unusedPrivateMethods);
    }
  private:
    // print warnings "unused ..."
    void WarnUnused(ASTContext &ctx, ClassSet &undefined, MethodSet &unused) {
      DiagnosticsEngine &diags = ctx.getDiagnostics();

      for (MethodSet::iterator I = unused.begin(), E = unused.end();
          I != E; ++I) {
        const CXXMethodDecl *m = *I;

        // care only about fully defined classes
        if (!IsDefinedWithFriends(ctx, undefined, m->getParent()))
          continue;

        // some people declare private never used ctors/dtors purposefully
        if (dyn_cast<CXXConstructorDecl>(m) || dyn_cast<CXXDestructorDecl>(m))
          continue;
        
        PrintUnusedWarning(diags, m);
      }
    }

    // if the class is defined and its friend functions/friend classes' methods
    // are all defined
    bool IsDefinedWithFriends(ASTContext &ctx, ClassSet &undefined,
        const CXXRecordDecl *r) {
      if (!IsDefined(ctx, undefined, r))
        return false;

      // whether all friends are defined
      for (CXXRecordDecl::friend_iterator I = r->friend_begin(),
          E = r->friend_end(); I != E; ++I) {
        // it may be a function...
        const NamedDecl *fDecl = (*I)->getFriendDecl();
        const FunctionDecl *fFun = dyn_cast_or_null<FunctionDecl>(fDecl);
        // ...or a type
        const TypeSourceInfo *fInfo = (*I)->getFriendType();
        const Type *fType;

        if (fFun) {
          if (!fFun->getCanonicalDecl()->isDefined())
            return false;
        } else if (fInfo && (fType = fInfo->getType().getTypePtrOrNull())) {
          const CXXRecordDecl *fRec = fType->getPointeeCXXRecordDecl();

          if (fRec && !IsDefined(ctx, undefined, fRec))
            return false;
        }
      }
      return true;
    }

    // if the class has body definition and all the class methods are defined
    bool IsDefined(ASTContext &ctx, ClassSet &undefined,
        const CXXRecordDecl *r) {
      // if the user just declared the class existence and provided no body,
      // then we haven't marked it as undefined yet
      if (!r->hasDefinition())
        return false;

      llvm::errs() << "asked if defined: " << r->getName() << "\n";

      // check in the set of undefined
      return !Contains(ctx, undefined, ctx.getRecordType(r));
    }

    void PrintUnusedWarning(DiagnosticsEngine &diags, const CXXMethodDecl *m) {
      unsigned diagId = diags.getCustomDiagID(DiagnosticsEngine::Warning,
          "private method %0 seems to be unused");
      diags.Report(m->getLocation(), diagId) << m->getQualifiedNameAsString();
    }
};

// main plugin action
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
