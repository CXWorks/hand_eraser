/*
 * Copyright (C) 2019 Xiang Cheng
 */

#include <iostream>
#include <cassert>
#include <unordered_set>
#include "llvm/Support/raw_ostream.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Pass.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#define built_in_func_num  (7 + 7)

using namespace llvm;
using namespace std;

typedef unsigned char uchar;

typedef unsigned int uint;

typedef unsigned short ushort;

typedef unsigned long ulong;

typedef unsigned long long ull;

typedef uint opcode;

typedef char int8;
typedef short int16;
typedef int int32;
typedef long long int64;

class EraserFunctionInstr: public FunctionPass{
protected:
    string math_funcs[21] = {"llvm.acos", "llvm.asin", "llvm.atan", "llvm.atan2", "llvm.cos", "llvm.cosh", "llvm.sin",
                             "llvm.sinh", "llvm.tanh", "llvm.exp", "llvm.frexp", "llvm.idexp", "llvm.log", "llvm.log10", "llvm.modf", "llvm.pow",
                             "llvm.sqrt", "llvm.ceil", "llvm.fabs", "llvm.floor", "llvm.fmod"};
    map<string, int> tt_func;
    raw_os_ostream rawOstream;
    Function* initEraser;
    Function* destroyEraser;

    Function* lockAccess;



    Function* lockRAccess;



    Function* onMalloc;



    Function* onFree;



    Function* memoryAccess;





    bool is_tt_func(StringRef s) {
        for(auto it = tt_func.begin(); it != tt_func.end(); it++){
            if (s.contains_lower(it->first)){
                return true;
            }
        }
        return false;
    }

    string built_in_func_name[built_in_func_num] = {
            "initEraser"
            , "destoryEraser"
            , "lockAccess"
            , "lockRAccess"
            , "onMalloc"
            , "onFree"
            , "memoryAccess"
    };
public:
    static char ID;
    LLVMContext* context;
    map<StringRef, vector<Value*>> param_map;
    map<StringRef, Value*> ret_map;
    map<StringRef, map<CallInst*, vector<StoreInst*>>> func_call_meta;
    map<StringRef, map<CallInst*, LoadInst*>> func_call_result_meta;
    map<StringRef, map<ReturnInst*, StoreInst*>> func_ret_meta;
    map<StringRef, vector<Instruction*>> insts;
    map<StringRef, CallInst*> func_call_byval;
    vector<CallInst*> to_be_inline;
    map<StringRef, AllocaInst*> func_va_tls;

    EraserFunctionInstr() : rawOstream(cout), FunctionPass(ID) {
        for (string s:math_funcs){
            tt_func[s] = 1;
        }
    }

    StringRef get_func_name(CallInst* callInst) {
        if (callInst->getCalledFunction()!= nullptr){
            return callInst->getCalledFunction()->getName();
        }else {

            Value* v = callInst->getCalledValue();
            if(auto fcn = dyn_cast<Function>(v->stripPointerCasts())){
                return fcn->getName();
            }
            return "";
        }
    }

    bool should_be_target(Function& F){
        for (int i = 0; i < built_in_func_num; ++i) {
            if (F.getName().startswith("_Z") || F.getName().contains("llvm.") ||  F.getName().startswith("__") || F.isDeclaration())
                return false;
        }
        return true;
    }

    unsigned int type_encode(Type* v, int shift = 0){
        if(shift>=28)
            return 0;
        unsigned int ans = v->getTypeID();
        if(ans == 11){
            switch (v->getIntegerBitWidth()){
                case 8:
                    ans = 17;
                    break;
                case 16:
                    ans = 18;
                    break;
                case 32:
                    ans = 19;
                    break;
                case 64:
                    ans = 20;
                    break;
            }
        }
        ans = ans<<shift;
        if(ans == Type::TypeID::PointerTyID)
            return ans | type_encode(v->getPointerElementType(), shift + 5);
        else
            return ans;
    }


    bool doInitialization(Module &M) {
        Module::FunctionListType& functionListType = M.getFunctionList();
        context = &M.getContext();
        for(auto func = functionListType.begin(); func!=functionListType.end();func++){
            if(func->getName().contains("initEraser")){
                initEraser = M.getFunction(func->getName());
            }
            if(func->getName().contains("destroyEraser")){
                destroyEraser = M.getFunction(func->getName());
            }

            if(func->getName().contains("onMalloc")){
                onMalloc = M.getFunction(func->getName());
            }
            if(func->getName().contains("onFree")){
                onFree = M.getFunction(func->getName());
            }
            if(func->getName().contains("memoryAccess")){
                memoryAccess = M.getFunction(func->getName());
            }
            if(func->getName().contains("lockAccess")){
                lockAccess = M.getFunction(func->getName());
            }
            if(func->getName().contains("lockRAccess")){
                lockRAccess = M.getFunction(func->getName());
            }

        }
        for(auto it = M.begin(); it != M.end(); it++){
            Function& F = *it;
            if(should_be_target(F)){
                removeUnreachableBlocks(F);
                findInst(F);
            }
        }

        return true;
    }




    bool runOnFunction(Function &F) override {
        LLVMContext & llvmContext = F.getContext();
        for (int i = 0; i < built_in_func_num; ++i) {
//            cout<<F.getName().str()<<endl;
            if(!should_be_target(F))
                return true;
        }
        //instr for all memory access
        if(F.getBasicBlockList().size() != 0){
            instrLLVMInst(F);
            cout<<F.getName().str()<<endl;
        }
        if(F.getName().equals("main")){
            //init eraser
            Instruction* firstInst = &F.getEntryBlock().front();
            Instruction* lastInst = &F.getBasicBlockList().back().back();
            if(auto lastI = dyn_cast<UnreachableInst>(lastInst)){
                lastInst = lastInst->getPrevNode();
            }
            Instruction * callInitEarser = CallInst::Create(initEraser,"",firstInst);
            Instruction * callDestoryEarser = CallInst::Create(destroyEraser,"", lastInst);
        }
        return true;
    }


    void findInst(Function& F){
        vector<Instruction*> to_be_instr;
        for (BasicBlock *bb : depth_first(&F.getEntryBlock())){
            for(auto inst = bb->begin();inst != bb->end();inst++){

                if( auto I = dyn_cast<PHINode>(inst)){
                    to_be_instr.push_back(I);
                }
                else if( auto I = dyn_cast<ReturnInst>(inst)){
                    to_be_instr.push_back(I);
                }
                else if( auto I = dyn_cast<LoadInst>(inst)){
                    to_be_instr.push_back(I);
                }
                else if( auto I = dyn_cast<StoreInst>(inst)){
                    to_be_instr.push_back(I);
                }
                else if(auto I = dyn_cast<CallInst>(inst)){
                    to_be_instr.push_back(I);
                }
            }
        }
        insts[F.getName()] = to_be_instr;
    }

    void instrLLVMInst(Function& F){
        LLVMContext & llvmContext = F.getContext();
        vector<Instruction*> to_be_instr;
        if(insts.count(F.getName()))
            to_be_instr = insts[F.getName()];
        else
            return;
        int half = 0;
        int binop_in = 0;
        //second round loop to make changes
        for(Instruction* inst : to_be_instr){
            binop_in = 0;
            if( auto I = dyn_cast<LoadInst>(inst)){
                Instruction* point= I->getNextNode();
                vector<Value*> args;
                int num = 0;
                args.push_back(ConstantInt::get(memoryAccess->getFunctionType()->getFunctionParamType(0), 1));
                Value * loc = I->getOperand(0);
                Value* casted = CastInst::CreateBitOrPointerCast(loc, memoryAccess->getFunctionType()->getFunctionParamType(1), "", point);
                args.push_back(casted);
                ArrayRef<Value*> args_arr(args);
                CallInst* callonStore = nullptr;
                callonStore = CallInst::Create(memoryAccess, args_arr,"",point);
            }
            if( auto I = dyn_cast<StoreInst>(inst)){
                Instruction* point= I->getNextNode();
                vector<Value*> args;
                int num = 0;

                args.push_back(ConstantInt::get(memoryAccess->getFunctionType()->getFunctionParamType(0), 0));
                Value * loc = I->getOperand(1);
                Value* casted = CastInst::CreateBitOrPointerCast(loc, memoryAccess->getFunctionType()->getFunctionParamType(1), "", point);
                args.push_back(casted);
                ArrayRef<Value*> args_arr(args);
                CallInst* callonStore = nullptr;
                callonStore = CallInst::Create(memoryAccess, args_arr,"",point);
            }
            //call function
            if(auto I = dyn_cast<CallInst>(inst)){
                if(get_func_name(I).equals("pthread_mutex_lock")){
                    Instruction* point= I->getNextNode();
                    vector<Value*> args;
                    int num = 0;
                    args.push_back(ConstantInt::get(lockAccess->getFunctionType()->getFunctionParamType(0), 0));
                    Value* loc = I->getArgOperand(1-1);
                    Value* casted = CastInst::CreateBitOrPointerCast(loc, lockAccess->getFunctionType()->getFunctionParamType(1), "", point);
                    args.push_back(casted);
                    ArrayRef<Value*> args_arr(args);
                    CallInst* callonLock = nullptr;
                    callonLock = CallInst::Create(lockAccess, args_arr,"",point);
                }
                if(get_func_name(I).equals("pthread_mutex_unlock")){
                    Instruction* point= I;
                    vector<Value*> args;
                    int num = 0;
                    args.push_back(ConstantInt::get(lockAccess->getFunctionType()->getFunctionParamType(0), 1));
                    Value* loc = I->getArgOperand(1-1);
                    Value* casted = CastInst::CreateBitOrPointerCast(loc, lockAccess->getFunctionType()->getFunctionParamType(1), "", point);
                    args.push_back(casted);
                    ArrayRef<Value*> args_arr(args);
                    CallInst* callonLock = nullptr;
                    callonLock = CallInst::Create(lockAccess, args_arr,"",point);
                }
                if(get_func_name(I).equals("pthread_rwlock_wrlock")){
                    Instruction* point= I->getNextNode();
                    vector<Value*> args;
                    int num = 0;
                    args.push_back(ConstantInt::get(lockAccess->getFunctionType()->getFunctionParamType(0), 0));
                    Value* loc = I->getArgOperand(1-1);
                    Value* casted = CastInst::CreateBitOrPointerCast(loc, lockAccess->getFunctionType()->getFunctionParamType(1), "", point);
                    args.push_back(casted);
                    ArrayRef<Value*> args_arr(args);
                    CallInst* callonLock = nullptr;
                    callonLock = CallInst::Create(lockAccess, args_arr,"",point);
                }
                if(get_func_name(I).equals("pthread_rwlock_unlock")){
                    Instruction* point= I;
                    vector<Value*> args;
                    int num = 0;
                    args.push_back(ConstantInt::get(lockAccess->getFunctionType()->getFunctionParamType(0), 1));
                    Value* loc = I->getArgOperand(1-1);
                    Value* casted = CastInst::CreateBitOrPointerCast(loc, lockAccess->getFunctionType()->getFunctionParamType(1), "", point);
                    args.push_back(casted);
                    ArrayRef<Value*> args_arr(args);
                    CallInst* callonLock = nullptr;
                    callonLock = CallInst::Create(lockAccess, args_arr,"",point);
                }
                if(get_func_name(I).equals("pthread_rwlock_rdlock")){
                    Instruction* point= I->getNextNode();
                    vector<Value*> args;
                    args.push_back(ConstantInt::get(lockRAccess->getFunctionType()->getFunctionParamType(0), 0));
                    Value* loc = I->getArgOperand(1-1);
                    Value* casted = CastInst::CreateBitOrPointerCast(loc, lockRAccess->getFunctionType()->getFunctionParamType(1), "", point);
                    args.push_back(casted);
                    ArrayRef<Value*> args_arr(args);
                    CallInst* callonLock = nullptr;
                    callonLock = CallInst::Create(lockRAccess, args_arr,"",point);
                }
                if(get_func_name(I).equals("malloc")){
                    Instruction* point= I->getNextNode();
                    vector<Value*> args;
                    Value * ans = I;
                    Value* casted = CastInst::CreateBitOrPointerCast(ans, onMalloc->getFunctionType()->getFunctionParamType(0), "", point);
                    args.push_back(casted);
                    Value* size = I->getArgOperand(1-1);
                    Value* casted2= CastInst::CreateIntegerCast(size, onMalloc->getFunctionType()->getFunctionParamType(1), true, "",  point);
                    args.push_back(casted2);
                    ArrayRef<Value*> args_arr(args);
                    CallInst* callonMalloc = nullptr;
                    callonMalloc = CallInst::Create(onMalloc, args_arr,"",point);
                }
                if(get_func_name(I).equals("free")){
                    Instruction* point= I->getNextNode();
                    vector<Value*> args;
                    Value* ptr = I->getArgOperand(1-1);
                    Value* casted = CastInst::CreateBitOrPointerCast(ptr, onFree->getFunctionType()->getFunctionParamType(0), "", point);
                    args.push_back(casted);
                    ArrayRef<Value*> args_arr(args);
                    CallInst* callonFree = nullptr;
                    callonFree = CallInst::Create(onFree, args_arr,"",point);
                }
            }
        }
    }




};

char EraserFunctionInstr::ID = 0;
static cl::opt<string> InputFilename("lib_file", cl::desc("Specify input filename for mypass"), cl::value_desc("filename"));
static RegisterPass<EraserFunctionInstr> X("eraser", "Do Instr",
                                           false /* Only looks at CFG */,
                                           false /* Analysis Pass */);