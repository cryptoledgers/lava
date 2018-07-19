#include <stdio.h>
#include <iostream>
#include <fstream>

#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchersInternal.h"
#include "clang/ASTMatchers/ASTMatchersMacros.h"

#include <iostream>

using namespace clang::tooling;
using namespace llvm;using namespace clang;
using namespace clang;
using namespace clang::ast_matchers;

using namespace std;


static cl::OptionCategory LavaFnCategory("LAVA Function diagnosis");
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::extrahelp MoreHelp(
    "\nIdentify all fn defs, prototypes, calls for later use by LAVA.\n");


ofstream outfile;


void spit_type(const char *label, QualType qt) {
    outfile << label << qt.getAsString() << "\n";
}

void spit_fun_decl(const FunctionDecl *fundecl) {
    outfile << "   fundecl: \n";
    if (fundecl->getStorageClass() == SC_Extern) 
        outfile << "      extern: True\n";
    else
        outfile << "      extern: False\n";
    spit_type( "      ret_type: ", fundecl->getReturnType());
    outfile << "      params: \n";
    for (auto p : fundecl->parameters()) { 
        QualType ot = p->getOriginalType();
        const Type *otp = ot.getTypePtr();
        if (otp->isFunctionType() || otp->isFunctionPointerType()) {
            spit_type("         - param: fnptr ", ot); 
        }
        else {
            spit_type("         - param: ", ot); 
        }
    }    
}


void spit_source_locs(const char *spaces, const Expr *expr, const SourceManager &sm) {
    auto sl1 = expr->getLocStart();
    auto sl2 = expr->getLocEnd();
    outfile << (string(spaces) + "start: ") << sl1.printToString(sm) << "\n"; 
    outfile << (string(spaces) + "end: ") << sl2.printToString(sm) << "\n"; 
}


class CallPrinter : public MatchFinder::MatchCallback {
    public :
    virtual void run(const MatchFinder::MatchResult &Result) {
        const CallExpr *call = Result.Nodes.getNodeAs<clang::CallExpr>("callExpr");
        if (call) {
            outfile << "- call: \n";
            spit_source_locs("   ", call, *Result.SourceManager);


            const FunctionDecl *func = call->getDirectCallee();            
            if (func == nullptr || func->getLocation().isInvalid()) {
                // its a call via fn pointer
                outfile << "   fnptr: true\n"; 
                outfile << "   name: None\n";
            }
            else {
                outfile << "   fnptr: false\n";
                outfile << "   name: " << func->getNameInfo().getAsString() << "\n";
            }                        
            QualType rt = call->getCallReturnType();//(Result.Context);
            spit_type( "   ret_type: ", rt);
            outfile << "   args: \n";
            for (auto it = call->arg_begin(); it != call->arg_end(); ++it) {
                const Expr *arg = dyn_cast<Expr>(*it);
                arg = arg->IgnoreImpCasts();
                QualType at = arg->IgnoreImpCasts()->getType();
                const Type *atp = at.getTypePtr();
                string expstr, type, info;
                outfile << "      - arg: \n";
                if (atp->isFunctionType()) {
                    const DeclRefExpr *dre = dyn_cast<DeclRefExpr>(arg);
                    outfile << "         name: " << dre->getNameInfo().getName().getAsString() << "\n";
                    outfile << "         type: " << at.getAsString() << "\n";
                    outfile << "         info: function\n";
                }
                else if (atp->isFunctionPointerType()) {
                    const DeclRefExpr *dre = dyn_cast<DeclRefExpr>(arg);
                    if (dre) 
                        outfile << "         name: " << dre->getNameInfo().getName().getAsString() << "\n";
                    else 
                        outfile << "         name: None\n";
                    outfile << "         type: " << at.getAsString() << "\n";
                    outfile << "         info: functionpointer\n";
                }
                else {
                    outfile << "         name: None\n";
                    outfile << "         type: " << at.getAsString() << "\n";
                    outfile << "         info: None\n";
                }
            }
        }
        
    }
};


class FnPtrAssignmentPrinter : public MatchFinder::MatchCallback {
    public :
    virtual void run(const MatchFinder::MatchResult &Result) {
        const BinaryOperator *bo = Result.Nodes.getNodeAs<BinaryOperator>("bo");
        Expr *rhs = bo->getRHS()->IgnoreImpCasts();
        const Type *rhst = rhs->getType().getTypePtr();
        if (rhst->isFunctionType()) {
            outfile << "- fnPtrAssign: \n";
            spit_source_locs("   ", bo, *Result.SourceManager);
            const DeclRefExpr *dre = llvm::dyn_cast<DeclRefExpr>(rhs);
            outfile << "   name: " << dre->getNameInfo().getAsString() << "\n";
            const ValueDecl *vd = dre->getDecl();
            const FunctionDecl *fndecl = llvm::dyn_cast<FunctionDecl>(vd);
            spit_fun_decl(fndecl);
        }                
    }
};


class VarDeclPrinter : public MatchFinder::MatchCallback {
    public :
    virtual void run(const MatchFinder::MatchResult &Result) {
        const VarDecl *vd = Result.Nodes.getNodeAs<VarDecl>("vd");
        const Type *et = vd->getType().getTypePtr();
        if (vd->hasInit() && et->isPointerType()) {
            const Expr *init = vd->getInit()->IgnoreImpCasts();
            const Type *it = init->getType().getTypePtr();
            if (it->isFunctionType()) {
                outfile << "- fnPtrAssign:\n";
                auto sl1 = vd->getLocStart();
                auto sl2 = vd->getLocEnd();
                outfile << "   start: " << sl1.printToString(*Result.SourceManager) << "\n";
                outfile << "   end: " << sl2.printToString(*Result.SourceManager) << "\n";
                const DeclRefExpr *dre = llvm::dyn_cast<DeclRefExpr>(init);
                outfile << "   name: " << dre->getNameInfo().getAsString() << "\n";
                const FunctionDecl *fndecl = llvm::dyn_cast<FunctionDecl>(dre->getDecl());
                spit_fun_decl(fndecl);
            }
        }
    }
};



class FunctionPrinter : public MatchFinder::MatchCallback {
    public:
    virtual void run(const MatchFinder::MatchResult &Result) {
        const FunctionDecl *func = 
            Result.Nodes.getNodeAs<FunctionDecl>("funcDecl");
//        if (func->isExternC()) return;
        if (func) {
            outfile << "- fun: \n";
            auto sl1 = func->getLocStart();
            auto sl2 = func->getLocEnd();
            outfile << "   start: " << sl1.printToString(*Result.SourceManager) << "\n";    
            outfile << "   end: " << sl2.printToString(*Result.SourceManager) << "\n";    
            outfile << "   name: " << (func->getNameInfo().getAsString()) << "\n";
            if (func->doesThisDeclarationHaveABody())  
                outfile << "   hasbody: true\n";
            else
                outfile << "   hasbody: false\n";
            spit_fun_decl(func);
      }        
    }
};



int main(int argc, const char **argv) {

    string outfilename = string(argv[argc-1]) + ".fn";

    if (outfilename == "--.fn")
        outfilename = "foo.fn";
    cout << "outfilename = [" << outfilename << "]\n";
    outfile.open (outfilename);
    
    CommonOptionsParser OptionsParser(argc, argv, LavaFnCategory);
    ClangTool Tool(OptionsParser.getCompilations(),
                   OptionsParser.getSourcePathList());
    MatchFinder Finder;
    CallPrinter CPrinter;
    FunctionPrinter FPrinter;
    FnPtrAssignmentPrinter FPAPrinter;
    VarDeclPrinter VDPrinter;

    Finder.addMatcher(
        callExpr().bind("callExpr"),
        &CPrinter);

    Finder.addMatcher(
        functionDecl().bind("funcDecl"),
        &FPrinter);

    Finder.addMatcher(
        binaryOperator(hasOperatorName("=")).bind("bo"),
        &FPAPrinter);

    Finder.addMatcher(
        varDecl().bind("vd"),
        &VDPrinter);
        
    int rv = Tool.run(newFrontendActionFactory(&Finder).get());

    outfile.close();
    return rv;
}

