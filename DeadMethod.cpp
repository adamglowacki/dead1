//
// Clang plugin: dead-method
// Author: Adam Głowacki
// ----------------------------------------------------------------------------
// Detects unused private methods in classes. Omits classes that are not fully
// defined in the current translation unit and these whose friends are not
// defined here.
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
    DeclRemover(MethodSet *privateOnes) : unused(privateOnes) { }

    bool VisitMemberExpr(MemberExpr *e) {
      const ValueDecl *d = e->getMemberDecl();
      FlagMethodUsed(dyn_cast_or_null<CXXMethodDecl>(d));
      return true;
    }

  private:
    MethodSet *unused;

    // remove the method from the unused methods set; ignore NULL silently
    void FlagMethodUsed(const CXXMethodDecl *m) {
      if (!m || !(m = m->getCanonicalDecl()))
        return;

      unused->erase(m);
    }
};

// gather:
//  - classes with undefined methods
//  - declared private methods
class DeclCollector : public RecursiveASTVisitor<DeclCollector> {
  public:
    DeclCollector(ASTContext *c, ClassSet *u, MethodSet *p, bool t)
      : ctx(c), undefinedClasses(u), privateMethods(p), templates(t) { }

    bool VisitCXXMethodDecl(CXXMethodDecl *m) {
      const CXXRecordDecl *r;
      m = m->getCanonicalDecl();
      if (!m || !(r = m->getParent()) || !(r = r->getCanonicalDecl()))
        return true;

      if (!m->isDefined())
        MarkUndefined(r);

      if (m->getAccess() == AS_private)
        if (!IsTemplated(m) || templates)
          privateMethods->insert(m);

      return true;
    }

    bool VisitCXXRecordDecl(CXXRecordDecl *r) {
      r = r->getCanonicalDecl();
      if (r && !r->hasDefinition())
        MarkUndefined(r);

      return true;
    }
  private:
    ASTContext *ctx;
    ClassSet *undefinedClasses;
    MethodSet *privateMethods;
    bool templates;

    void MarkUndefined(const CXXRecordDecl *r) {
      Insert(*ctx, *undefinedClasses, ctx->getRecordType(r));
    }

    bool IsTemplated(const CXXMethodDecl *m) {
      if (m->getDescribedFunctionTemplate())
        return true;
      return false;
    }
};

// deal with every translation unit separately
class DeadConsumer : public ASTConsumer {
  public:
    DeadConsumer(bool includeTemplateMethods)
      : templatesAlso(includeTemplateMethods) { }

    virtual void HandleTranslationUnit(ASTContext &ctx) {
      MethodSet unusedPrivateMethods;
      ClassSet undefinedClasses;
      TranslationUnitDecl *tuDecl = ctx.getTranslationUnitDecl();

      // gather lists of:
      //  - not fully defined classes
      //  - all the private methods
      DeclCollector collector(&ctx, &undefinedClasses, &unusedPrivateMethods,
          templatesAlso);
      collector.TraverseDecl(tuDecl);

      DeclRemover remover(&unusedPrivateMethods);
      remover.TraverseDecl(tuDecl);

      WarnUnused(ctx, undefinedClasses, unusedPrivateMethods);
    }
  private:
    // whether user shall be informed about (possibly) unused templated methods
    bool templatesAlso;
    // print warnings "unused ..."
    void WarnUnused(ASTContext &ctx, ClassSet &undefined, MethodSet &unused) {
      DiagnosticsEngine &diags = ctx.getDiagnostics();

      for (MethodSet::iterator I = unused.begin(), E = unused.end();
          I != E; ++I) {
        const CXXMethodDecl *m = *I;

        // care only about fully defined classes
        if (!IsDefined(ctx, undefined, m->getParent()))
          continue;

        // some people declare private never used ctors/dtors purposefully
        if (dyn_cast<CXXConstructorDecl>(m) || dyn_cast<CXXDestructorDecl>(m))
          continue;
        
        MakeUnusedWarning(diags, m);
      }
    }

    // if the class is defined and its friend functions/friend classes' methods
    // are all defined
    bool IsDefined(ASTContext &ctx, ClassSet &undefined,
        const CXXRecordDecl *r) {
      if (Contains(ctx, undefined, ctx.getRecordType(r)))
        return false;

      // whether all friends are defined
      for (CXXRecordDecl::friend_iterator I = r->friend_begin(),
          E = r->friend_end(); I != E; ++I) {
        // it may be a function...
        const NamedDecl *fDecl = (*I)->getFriendDecl();
        const FunctionDecl *fFun = dyn_cast_or_null<FunctionDecl>(fDecl);
        if (fFun) {
          if (!fFun->getCanonicalDecl()->isDefined())
            return false;
        }

        // ...or a type
        const TypeSourceInfo *fInfo = (*I)->getFriendType();
        if (fInfo) {
          if (Contains(ctx, undefined,  fInfo->getType()))
            return false;
        }
      }
      // nothing suspicious found
      return true;
    }

    void MakeUnusedWarning(DiagnosticsEngine &diags, const CXXMethodDecl *m) {
      unsigned diagId = diags.getCustomDiagID(DiagnosticsEngine::Warning,
          "private method %0 seems to be unused");
      diags.Report(m->getLocation(), diagId) << m->getQualifiedNameAsString();
    }
};

// main plugin action
class DeadAction : public PluginASTAction {
  protected:
    ASTConsumer *CreateASTConsumer(CompilerInstance &, StringRef) {
      return new DeadConsumer(includeTemplateMethods);
    }

    bool ParseArgs(const CompilerInstance &ci,
        const std::vector<std::string> &args) {
      includeTemplateMethods = false;
      bool showHelp = false;

      DiagnosticsEngine &diags = ci.getDiagnostics();
      for (unsigned i = 0, e = args.size(); i != e; ++i)
        if (args[i] == "include-template-methods")
          includeTemplateMethods = true;
        else if (args[i] == "help")
          showHelp = true;
        else {
          MakeArgumentError(diags, args[i]);
          return false;
        }

      if (showHelp)
        ShowHelp();
      return true;
    }
  private:
    bool includeTemplateMethods;
    void MakeArgumentError(DiagnosticsEngine &diags, std::string arg) {
      unsigned diagId = diags.getCustomDiagID(DiagnosticsEngine::Error,
          "invalid argument '" + arg + "'");
      diags.Report(diagId);
    }
    void ShowHelp() {
      llvm::errs() << "DeadMethod plugin: warn if fully defined classes "
        "with unused private methods found\n"
        "Available arguments:\n"
        "  help                      print this message\n"
        "  include-template-methods  look for template methods as well\n";
    }
};
}

// register the plugin
static FrontendPluginRegistry::Add<DeadAction>
X("dead-method", "look for unused private methods");
