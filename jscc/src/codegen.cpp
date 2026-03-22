#include "codegen.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/CodeGen/TargetPassConfig.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <cstring>

#include <iostream>
#include <fstream>

namespace jscc {

// ── constructor ───────────────────────────────────────────────────────────────

Codegen::Codegen(const std::string& module_name, ErrorReporter& reporter)
    : m_reporter(reporter) {
    m_ctx     = std::make_unique<llvm::LLVMContext>();
    m_module  = std::make_unique<llvm::Module>(module_name, *m_ctx);
    m_builder = std::make_unique<llvm::IRBuilder<>>(*m_ctx);

    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    declare_stdlib_intrinsics();
}

// ── type mapping ──────────────────────────────────────────────────────────────

llvm::Type* Codegen::llvm_type(const std::string& name) {
    if (name == "i32" || name == "int")   return llvm::Type::getInt32Ty(*m_ctx);
    if (name == "i64")                    return llvm::Type::getInt64Ty(*m_ctx);
    if (name == "i8"  || name == "u8")    return llvm::Type::getInt8Ty(*m_ctx);
    if (name == "i16" || name == "u16")   return llvm::Type::getInt16Ty(*m_ctx);
    if (name == "u32")                    return llvm::Type::getInt32Ty(*m_ctx);
    if (name == "u64")                    return llvm::Type::getInt64Ty(*m_ctx);
    if (name == "f32" || name == "float") return llvm::Type::getFloatTy(*m_ctx);
    if (name == "f64")                    return llvm::Type::getDoubleTy(*m_ctx);
    if (name == "bool")                   return llvm::Type::getInt1Ty(*m_ctx);
    if (name == "void")                   return llvm::Type::getVoidTy(*m_ctx);
    if (name == "str")                    return llvm::PointerType::getUnqual(
                                              llvm::Type::getInt8Ty(*m_ctx));
    // default unknown types to i32
    return llvm::Type::getInt32Ty(*m_ctx);
}

llvm::Type* Codegen::llvm_type_from_ref(const TypeRef& t) {
    if (!t || t->is_unknown()) return llvm::Type::getInt32Ty(*m_ctx);
    return llvm_type(t->name);
}

// ── stdlib intrinsics ─────────────────────────────────────────────────────────
// declare C standard library functions we call for jade.stdlib.io etc.

void Codegen::declare_stdlib_intrinsics() {
    auto i8ptr  = llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(*m_ctx));
    auto i32    = llvm::Type::getInt32Ty(*m_ctx);
    auto voidty = llvm::Type::getVoidTy(*m_ctx);

    // printf(fmt, ...) -> i32
    auto printf_ty = llvm::FunctionType::get(i32, {i8ptr}, true);
    m_module->getOrInsertFunction("printf", printf_ty);

    // puts(str) -> i32
    auto puts_ty = llvm::FunctionType::get(i32, {i8ptr}, false);
    m_module->getOrInsertFunction("puts", puts_ty);

    // fflush(FILE*) -> i32  (we use null = flush stdout)
    auto fflush_ty = llvm::FunctionType::get(i32, {i8ptr}, false);
    m_module->getOrInsertFunction("fflush", fflush_ty);
}

llvm::Function* Codegen::get_or_declare(const std::string& name,
                                          llvm::FunctionType* ft) {
    auto* fn = m_module->getFunction(name);
    if (!fn) fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                          name, *m_module);
    return fn;
}

// ── helpers ───────────────────────────────────────────────────────────────────

llvm::Value* Codegen::i32(int v) {
    return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*m_ctx), v);
}
llvm::Value* Codegen::i1(bool v) {
    return llvm::ConstantInt::get(llvm::Type::getInt1Ty(*m_ctx), v ? 1 : 0);
}
llvm::Value* Codegen::f64(double v) {
    return llvm::ConstantFP::get(llvm::Type::getDoubleTy(*m_ctx), v);
}

llvm::AllocaInst* Codegen::create_entry_alloca(llvm::Function* fn,
                                                 const std::string& name,
                                                 llvm::Type* type) {
    llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
    return tmp.CreateAlloca(type, nullptr, name);
}

llvm::Value* Codegen::get_local(const std::string& name) {
    auto it = m_locals.find(name);
    if (it != m_locals.end()) return it->second;
    return nullptr;
}

void Codegen::set_local(const std::string& name, llvm::Value* val) {
    m_locals[name] = val;
}

// ── top level generate ────────────────────────────────────────────────────────

bool Codegen::generate(Program& program) {
    for (auto& stmt : program.stmts)
        gen_stmt(stmt.get());

    // verify the module
    std::string err;
    llvm::raw_string_ostream es(err);
    if (llvm::verifyModule(*m_module, &es)) {
        std::cerr << "LLVM module verification failed:\n" << err << "\n";
        return false;
    }
    return true;
}

// ── statement codegen ─────────────────────────────────────────────────────────

void Codegen::gen_stmt(Stmt* stmt) {
    if (!stmt) return;
    switch (stmt->kind) {
        case Stmt::Kind::PACKAGE:
        case Stmt::Kind::IMPORT:
        case Stmt::Kind::TYPE_ALIAS:
            break; // nothing to emit

        case Stmt::Kind::DEF:        gen_def(stmt);     break;
        case Stmt::Kind::VAL:
        case Stmt::Kind::VAR:        gen_val_var(stmt); break;
        case Stmt::Kind::RETURN:     gen_return(stmt);  break;
        case Stmt::Kind::IF:         gen_if(stmt);      break;
        case Stmt::Kind::WHILE:      gen_while(stmt);   break;
        case Stmt::Kind::FOR:        gen_for(stmt);     break;
        case Stmt::Kind::EXPR_STMT:
            if (stmt->expr) gen_expr(stmt->expr.get());
            break;

        case Stmt::Kind::DATA_CLASS:
        case Stmt::Kind::SEALED_CLASS:
        case Stmt::Kind::CLASS:
        case Stmt::Kind::TRAIT:
            // class bodies: only emit member functions for now
            for (auto& s : stmt->class_body)
                if (s->kind == Stmt::Kind::DEF) gen_def(s.get());
            break;

        default: break;
    }
}

void Codegen::gen_block(std::vector<StmtPtr>& body) {
    for (auto& s : body) gen_stmt(s.get());
}

// ── function codegen ──────────────────────────────────────────────────────────

void Codegen::gen_def(Stmt* stmt) {
    // build param types
    std::vector<llvm::Type*> param_types;
    for (auto& p : stmt->fn_params) {
        llvm::Type* pt = p.type
            ? llvm_type((*p.type)->name)
            : llvm::Type::getInt32Ty(*m_ctx);
        param_types.push_back(pt);
    }

    // return type — main() defaults to i32 if unannotated
    llvm::Type* ret_type;
    if (stmt->fn_return_type) {
        ret_type = llvm_type((*stmt->fn_return_type)->name);
    } else if (stmt->fn_name == "main") {
        ret_type = llvm::Type::getInt32Ty(*m_ctx);
    } else {
        ret_type = llvm::Type::getVoidTy(*m_ctx);
    }

    auto* fn_type = llvm::FunctionType::get(ret_type, param_types, false);
    auto* fn = llvm::Function::Create(fn_type,
                                       llvm::Function::ExternalLinkage,
                                       stmt->fn_name, *m_module);

    // name the params
    size_t i = 0;
    for (auto& arg : fn->args()) {
        if (i < stmt->fn_params.size())
            arg.setName(stmt->fn_params[i++].name);
    }

    // create entry block
    auto* entry = llvm::BasicBlock::Create(*m_ctx, "entry", fn);
    m_builder->SetInsertPoint(entry);

    // save/restore locals for nested functions
    auto saved_locals  = m_locals;
    auto saved_fn      = m_current_fn;
    m_current_fn       = fn;
    m_locals.clear();

    // alloca for each param
    i = 0;
    for (auto& arg : fn->args()) {
        auto* alloca = create_entry_alloca(fn, arg.getName().str(), arg.getType());
        m_builder->CreateStore(&arg, alloca);
        m_locals[arg.getName().str()] = alloca;
        i++;
    }

    // generate body
    if (stmt->fn_expr) {
        // single expression body: def foo() = expr
        auto* val = gen_expr(stmt->fn_expr.get());
        if (val && !ret_type->isVoidTy())
            m_builder->CreateRet(val);
        else
            m_builder->CreateRetVoid();
    } else {
        gen_block(stmt->fn_body);
        // implicit void return if no explicit return
        if (!m_builder->GetInsertBlock()->getTerminator()) {
            if (ret_type->isVoidTy())
                m_builder->CreateRetVoid();
            else
                m_builder->CreateRet(llvm::Constant::getNullValue(ret_type));
        }
    }

    m_locals     = saved_locals;
    m_current_fn = saved_fn;
}

// ── val/var codegen ───────────────────────────────────────────────────────────

void Codegen::gen_val_var(Stmt* stmt) {
    if (!m_current_fn) return; // top-level — skip for now

    // determine type from init expr or annotation
    llvm::Type* type = llvm::Type::getInt32Ty(*m_ctx);

    llvm::Value* init_val = nullptr;
    if (stmt->var_init) {
        init_val = gen_expr(stmt->var_init.get());
        if (init_val) type = init_val->getType();
    }

    if (stmt->var_type) {
        type = llvm_type((*stmt->var_type)->name);
    }

    auto* alloca = create_entry_alloca(m_current_fn, stmt->var_name, type);
    if (init_val) {
        // cast if needed (e.g. int literal to f64)
        if (init_val->getType() != type) {
            if (type->isDoubleTy() && init_val->getType()->isIntegerTy())
                init_val = m_builder->CreateSIToFP(init_val,
                               llvm::Type::getDoubleTy(*m_ctx), "cast");
            else if (type->isIntegerTy() && init_val->getType()->isDoubleTy())
                init_val = m_builder->CreateFPToSI(init_val,
                               llvm::Type::getInt32Ty(*m_ctx), "cast");
        }
        m_builder->CreateStore(init_val, alloca);
    }

    m_locals[stmt->var_name] = alloca;
}

// ── return codegen ────────────────────────────────────────────────────────────

void Codegen::gen_return(Stmt* stmt) {
    if (stmt->return_val) {
        auto* val = gen_expr(stmt->return_val->get());
        if (val) m_builder->CreateRet(val);
        else     m_builder->CreateRetVoid();
    } else {
        m_builder->CreateRetVoid();
    }
}

// ── if codegen ────────────────────────────────────────────────────────────────

void Codegen::gen_if(Stmt* stmt) {
    auto* cond = gen_expr(stmt->if_cond.get());
    if (!cond) return;

    // convert to bool if needed
    if (!cond->getType()->isIntegerTy(1))
        cond = m_builder->CreateICmpNE(cond,
                   llvm::Constant::getNullValue(cond->getType()), "ifcond");

    auto* fn       = m_builder->GetInsertBlock()->getParent();
    auto* then_bb  = llvm::BasicBlock::Create(*m_ctx, "then", fn);
    auto* else_bb  = llvm::BasicBlock::Create(*m_ctx, "else");
    auto* merge_bb = llvm::BasicBlock::Create(*m_ctx, "ifmerge");

    m_builder->CreateCondBr(cond, then_bb, else_bb);

    // then
    m_builder->SetInsertPoint(then_bb);
    gen_block(stmt->if_body);
    if (!m_builder->GetInsertBlock()->getTerminator())
        m_builder->CreateBr(merge_bb);

    // else
    fn->insert(fn->end(), else_bb);
    m_builder->SetInsertPoint(else_bb);
    if (!stmt->else_body.empty())
        gen_block(stmt->else_body);
    if (!m_builder->GetInsertBlock()->getTerminator())
        m_builder->CreateBr(merge_bb);

    // merge
    fn->insert(fn->end(), merge_bb);
    m_builder->SetInsertPoint(merge_bb);
}

// ── while codegen ─────────────────────────────────────────────────────────────

void Codegen::gen_while(Stmt* stmt) {
    auto* fn       = m_builder->GetInsertBlock()->getParent();
    auto* cond_bb  = llvm::BasicBlock::Create(*m_ctx, "whilecond", fn);
    auto* body_bb  = llvm::BasicBlock::Create(*m_ctx, "whilebody");
    auto* after_bb = llvm::BasicBlock::Create(*m_ctx, "whileafter");

    m_builder->CreateBr(cond_bb);

    // condition
    m_builder->SetInsertPoint(cond_bb);
    auto* cond = gen_expr(stmt->while_cond.get());
    if (!cond) return;
    if (!cond->getType()->isIntegerTy(1))
        cond = m_builder->CreateICmpNE(cond,
                   llvm::Constant::getNullValue(cond->getType()), "whilecond");
    m_builder->CreateCondBr(cond, body_bb, after_bb);

    // body
    fn->insert(fn->end(), body_bb);
    m_builder->SetInsertPoint(body_bb);
    gen_block(stmt->while_body);
    if (!m_builder->GetInsertBlock()->getTerminator())
        m_builder->CreateBr(cond_bb);

    // after
    fn->insert(fn->end(), after_bb);
    m_builder->SetInsertPoint(after_bb);
}

// ── for codegen ───────────────────────────────────────────────────────────────
// simplified: only handles numeric ranges (0..n) for now

void Codegen::gen_for(Stmt* stmt) {
    auto* fn       = m_builder->GetInsertBlock()->getParent();
    auto* i32ty    = llvm::Type::getInt32Ty(*m_ctx);

    // alloca for loop var
    auto* loop_var = create_entry_alloca(fn, stmt->for_var, i32ty);
    m_builder->CreateStore(llvm::ConstantInt::get(i32ty, 0), loop_var);
    m_locals[stmt->for_var] = loop_var;

    // get upper bound from iter expr
    auto* bound = gen_expr(stmt->for_iter.get());
    if (!bound) bound = llvm::ConstantInt::get(i32ty, 0);

    auto* cond_bb  = llvm::BasicBlock::Create(*m_ctx, "forcond", fn);
    auto* body_bb  = llvm::BasicBlock::Create(*m_ctx, "forbody");
    auto* after_bb = llvm::BasicBlock::Create(*m_ctx, "forafter");

    m_builder->CreateBr(cond_bb);

    // condition: i < bound
    m_builder->SetInsertPoint(cond_bb);
    auto* cur  = m_builder->CreateLoad(i32ty, loop_var, "loopvar");
    auto* cond = m_builder->CreateICmpSLT(cur, bound, "forcond");
    m_builder->CreateCondBr(cond, body_bb, after_bb);

    // body
    fn->insert(fn->end(), body_bb);
    m_builder->SetInsertPoint(body_bb);
    gen_block(stmt->for_body);

    // increment
    if (!m_builder->GetInsertBlock()->getTerminator()) {
        auto* next = m_builder->CreateAdd(
            m_builder->CreateLoad(i32ty, loop_var), i32(1), "inc");
        m_builder->CreateStore(next, loop_var);
        m_builder->CreateBr(cond_bb);
    }

    // after
    fn->insert(fn->end(), after_bb);
    m_builder->SetInsertPoint(after_bb);
}

// ── expression codegen ────────────────────────────────────────────────────────

llvm::Value* Codegen::gen_expr(Expr* expr) {
    if (!expr) return nullptr;

    switch (expr->kind) {
        case Expr::Kind::INT_LIT:
            return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*m_ctx),
                                           expr->int_val);
        case Expr::Kind::FLOAT_LIT:
            return llvm::ConstantFP::get(llvm::Type::getDoubleTy(*m_ctx),
                                          expr->float_val);
        case Expr::Kind::BOOL_LIT:
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(*m_ctx),
                                           expr->bool_val ? 1 : 0);
        case Expr::Kind::NULL_LIT:
            return llvm::Constant::getNullValue(llvm::Type::getInt32Ty(*m_ctx));

        case Expr::Kind::STRING_LIT:
            return m_builder->CreateGlobalStringPtr(expr->str_val, "str");
            break;

        case Expr::Kind::IDENT: {
            auto* alloca = get_local(expr->str_val);
            if (!alloca) return nullptr;
            // LLVM 18: use getAllocatedType() instead of getPointerElementType()
            auto* alloca_inst = llvm::dyn_cast<llvm::AllocaInst>(alloca);
            if (!alloca_inst) return alloca;
            return m_builder->CreateLoad(
                alloca_inst->getAllocatedType(), alloca,
                expr->str_val);
        }

        case Expr::Kind::ASSIGN: {
            auto* rhs = gen_expr(expr->right.get());
            if (!rhs) return nullptr;
            auto* alloca = get_local(expr->left->str_val);
            if (alloca) m_builder->CreateStore(rhs, alloca);
            return rhs;
        }

        case Expr::Kind::BINARY:  return gen_binary(expr);
        case Expr::Kind::UNARY:   return gen_unary(expr);
        case Expr::Kind::CALL:    return gen_call(expr);
        case Expr::Kind::METHOD_CALL: return gen_method_call(expr);
        case Expr::Kind::WHEN:    return gen_when(expr);

        case Expr::Kind::TUPLE: {
            // for now just evaluate all elements and return last
            llvm::Value* last = nullptr;
            for (auto& el : expr->elements)
                last = gen_expr(el.get());
            return last;
        }

        default:
            return nullptr;
    }
}

// ── binary op codegen ─────────────────────────────────────────────────────────

llvm::Value* Codegen::gen_binary(Expr* expr) {
    auto* L = gen_expr(expr->left.get());
    auto* R = gen_expr(expr->right.get());
    if (!L || !R) return nullptr;

    bool is_float = L->getType()->isDoubleTy() || R->getType()->isDoubleTy();

    // promote int to float if needed
    if (is_float) {
        if (L->getType()->isIntegerTy())
            L = m_builder->CreateSIToFP(L, llvm::Type::getDoubleTy(*m_ctx));
        if (R->getType()->isIntegerTy())
            R = m_builder->CreateSIToFP(R, llvm::Type::getDoubleTy(*m_ctx));
    }

    auto& op = expr->op;

    if (op == "+") return is_float ? m_builder->CreateFAdd(L, R, "fadd")
                                   : m_builder->CreateAdd(L, R, "add");
    if (op == "-") return is_float ? m_builder->CreateFSub(L, R, "fsub")
                                   : m_builder->CreateSub(L, R, "sub");
    if (op == "*") return is_float ? m_builder->CreateFMul(L, R, "fmul")
                                   : m_builder->CreateMul(L, R, "mul");
    if (op == "/") return is_float ? m_builder->CreateFDiv(L, R, "fdiv")
                                   : m_builder->CreateSDiv(L, R, "div");
    if (op == "%") return m_builder->CreateSRem(L, R, "rem");

    // comparisons
    if (op == "==") return is_float ? m_builder->CreateFCmpOEQ(L, R, "eq")
                                    : m_builder->CreateICmpEQ(L, R, "eq");
    if (op == "!=") return is_float ? m_builder->CreateFCmpONE(L, R, "ne")
                                    : m_builder->CreateICmpNE(L, R, "ne");
    if (op == "<")  return is_float ? m_builder->CreateFCmpOLT(L, R, "lt")
                                    : m_builder->CreateICmpSLT(L, R, "lt");
    if (op == ">")  return is_float ? m_builder->CreateFCmpOGT(L, R, "gt")
                                    : m_builder->CreateICmpSGT(L, R, "gt");
    if (op == "<=") return is_float ? m_builder->CreateFCmpOLE(L, R, "le")
                                    : m_builder->CreateICmpSLE(L, R, "le");
    if (op == ">=") return is_float ? m_builder->CreateFCmpOGE(L, R, "ge")
                                    : m_builder->CreateICmpSGE(L, R, "ge");

    if (op == "&&") return m_builder->CreateAnd(L, R, "and");
    if (op == "||") return m_builder->CreateOr(L, R, "or");

    return nullptr;
}

// ── unary op codegen ──────────────────────────────────────────────────────────

llvm::Value* Codegen::gen_unary(Expr* expr) {
    auto* val = gen_expr(expr->left.get());
    if (!val) return nullptr;

    if (expr->op == "!")
        return m_builder->CreateNot(val, "not");
    if (expr->op == "-") {
        if (val->getType()->isDoubleTy())
            return m_builder->CreateFNeg(val, "fneg");
        return m_builder->CreateNeg(val, "neg");
    }
    return val;
}

// ── call codegen ──────────────────────────────────────────────────────────────

llvm::Value* Codegen::gen_call(Expr* expr) {
    // look up function in module
    auto* fn = m_module->getFunction(expr->str_val);
    if (!fn) return nullptr;

    std::vector<llvm::Value*> args;
    for (auto& a : expr->args) {
        auto* v = gen_expr(a.get());
        if (v) args.push_back(v);
    }

    if (fn->getReturnType()->isVoidTy()) {
        m_builder->CreateCall(fn, args);
        return nullptr;
    }
    return m_builder->CreateCall(fn, args, "call");
}

// ── method call codegen ───────────────────────────────────────────────────────

llvm::Value* Codegen::gen_method_call(Expr* expr) {
    // special case: io.println / io.print -> printf
    auto* callee_expr = expr->callee.get();
    std::string module_name;

    if (callee_expr && callee_expr->kind == Expr::Kind::IDENT)
        module_name = callee_expr->str_val;

    auto& method = expr->method_name;

    // io.println(str) -> printf("%s\n", str)
    if ((module_name == "io" && method == "println") ||
        (module_name == "io" && method == "print")) {

        auto* printf_fn = m_module->getFunction("printf");
        if (!printf_fn) return nullptr;

        // build the format string
        std::string fmt = (method == "println") ? "%s\n" : "%s";
        auto* fmt_str = m_builder->CreateGlobalStringPtr(fmt, "fmt");

        std::vector<llvm::Value*> args = { fmt_str };
        for (auto& a : expr->args) {
            auto* v = gen_expr(a.get());
            if (v) args.push_back(v);
        }
        m_builder->CreateCall(printf_fn, args);
        return nullptr;
    }

    // await — just evaluate the callee expression
    if (method == "await") {
        return gen_expr(callee_expr);
    }

    // generic method call — evaluate callee and args, return null for now
    gen_expr(callee_expr);
    for (auto& a : expr->args) gen_expr(a.get());
    return nullptr;
}

// ── when codegen ──────────────────────────────────────────────────────────────
// simplified: evaluates subject, generates each arm sequentially

llvm::Value* Codegen::gen_when(Expr* expr) {
    gen_expr(expr->when_subject.get());

    // for each arm just gen the body — full pattern matching IR needs more work
    for (auto& arm : expr->when_arms) {
        gen_expr(arm.first.get());
        gen_expr(arm.second.get());
    }
    return nullptr;
}

// ── emit IR ───────────────────────────────────────────────────────────────────

void Codegen::dump_ir() {
    m_module->print(llvm::outs(), nullptr);
}

// ── emit object file ──────────────────────────────────────────────────────────

bool Codegen::emit_object(const std::string& output_path) {
    auto target_triple = llvm::sys::getDefaultTargetTriple();
    m_module->setTargetTriple(target_triple);

    std::string err;
    auto* target = llvm::TargetRegistry::lookupTarget(target_triple, err);
    if (!target) {
        std::cerr << "target error: " << err << "\n";
        return false;
    }

    auto cpu      = llvm::sys::getHostCPUName();
    auto features = "";
    llvm::TargetOptions opt;
    auto* tm = target->createTargetMachine(target_triple, cpu, features,
                                            opt, llvm::Reloc::PIC_);
    m_module->setDataLayout(tm->createDataLayout());

    std::error_code ec;
    llvm::raw_fd_ostream dest(output_path, ec, llvm::sys::fs::OF_None);
    if (ec) {
        std::cerr << "could not open output file: " << ec.message() << "\n";
        return false;
    }

    llvm::legacy::PassManager pass;
    if (tm->addPassesToEmitFile(pass, dest, nullptr,
                                 llvm::CodeGenFileType::ObjectFile)) {
        std::cerr << "target machine can't emit object file\n";
        return false;
    }

    pass.run(*m_module);
    dest.flush();
    return true;
}

// ── emit .joc file ────────────────────────────────────────────────────────────
//
// .joc binary layout:
//
//  [0..3]   magic        "JADE"
//  [4]      version      1
//  [5]      flags        bit0=has_bytecode  bit1=bytecode_only
//  [6..7]   reserved     0
//  [8..11]  num_slices   number of arch slices that follow
//  [12..15] meta_len     byte length of metadata string (incl. null terminator)
//  [16..16+meta_len-1]  metadata  (package name, null terminated)
//
//  slice directory (one entry per slice, immediately after metadata):
//    [0..3]  tag    "x86_" | "ARM_" | "LLBC"
//    [4..11] offset absolute byte offset into file
//    [8..15] size   byte length of slice data         (8 bytes each field)
//
//  slice data — packed end to end after the directory
//
// jvk reads the directory, picks the best slice for the running CPU,
// falls back to LLBC and JITs it if no native slice matches.
//
// --no-bytecode strips the LLBC slice (bit1 of flags set).

struct JocSlice {
    char     tag[4];   // "x86_", "ARM_", "LLBC"
    uint64_t offset;
    uint64_t size;
};

bool Codegen::emit_joc(const std::string& output_path,
                        const std::vector<std::string>& extra_targets,
                        bool include_bytecode,
                        bool bytecode_only) {

    // ── collect slices ────────────────────────────────────────────────────
    std::vector<JocSlice>          slice_meta;
    std::vector<std::vector<char>> slice_data;

    auto add_slice = [&](const char tag[4], std::vector<char> data) {
        JocSlice s;
        std::memcpy(s.tag, tag, 4);
        s.offset = 0; // filled in later
        s.size   = data.size();
        slice_meta.push_back(s);
        slice_data.push_back(std::move(data));
    };

    // helper: compile for a specific target triple -> raw object bytes
    auto compile_for_target = [&](const std::string& triple)
        -> std::vector<char> {

        std::string err;
        auto* target = llvm::TargetRegistry::lookupTarget(triple, err);
        if (!target) {
            std::cerr << "unknown target '" << triple << "': " << err << "\n";
            return {};
        }

        auto cpu     = (triple == llvm::sys::getDefaultTargetTriple())
                        ? llvm::sys::getHostCPUName().str()
                        : std::string("generic");
        llvm::TargetOptions opt;
        auto* tm = target->createTargetMachine(triple, cpu, "",
                                                opt, llvm::Reloc::PIC_);
        // clone module with this target's data layout
        auto clone = llvm::CloneModule(*m_module);
        clone->setTargetTriple(triple);
        clone->setDataLayout(tm->createDataLayout());

        llvm::SmallVector<char, 0> buf;
        llvm::raw_svector_ostream os(buf);
        llvm::legacy::PassManager pm;
        if (tm->addPassesToEmitFile(pm, os, nullptr,
                                     llvm::CodeGenFileType::ObjectFile)) {
            std::cerr << "target cannot emit object file\n";
            return {};
        }
        pm.run(*clone);
        return std::vector<char>(buf.begin(), buf.end());
    };

    // ── native slice(s) ───────────────────────────────────────────────────

    if (!bytecode_only) {
        // always compile for host
        auto host_triple = llvm::sys::getDefaultTargetTriple();
        m_module->setTargetTriple(host_triple);

        // determine arch tag from triple
        auto arch_tag = [](const std::string& triple) -> std::string {
            if (triple.find("x86_64") != std::string::npos) return "x86_";
            if (triple.find("aarch64") != std::string::npos ||
                triple.find("arm64")   != std::string::npos) return "ARM_";
            if (triple.find("riscv64") != std::string::npos) return "RV64";
            if (triple.find("wasm32")  != std::string::npos) return "WASM";
            return "GEN_";
        };

        auto host_obj = compile_for_target(host_triple);
        if (host_obj.empty()) return false;
        auto tag = arch_tag(host_triple);
        add_slice(tag.c_str(), std::move(host_obj));

        // extra --target slices
        for (auto& t : extra_targets) {
            auto obj = compile_for_target(t);
            if (obj.empty()) continue;
            add_slice(arch_tag(t).c_str(), std::move(obj));
        }
    }

    // ── LLBC bytecode slice ───────────────────────────────────────────────

    if (include_bytecode) {
        llvm::SmallVector<char, 0> bc_buf;
        llvm::raw_svector_ostream  bc_os(bc_buf);
        llvm::WriteBitcodeToFile(*m_module, bc_os);
        std::vector<char> bc(bc_buf.begin(), bc_buf.end());
        add_slice("LLBC", std::move(bc));
    }

    if (slice_meta.empty()) {
        std::cerr << "no slices to write\n";
        return false;
    }

    // ── build metadata ────────────────────────────────────────────────────

    std::string metadata = m_module->getName().str();
    uint32_t    meta_len = (uint32_t)metadata.size() + 1;

    // header size = 16 bytes fixed + meta_len
    // directory  = num_slices * (4 tag + 8 offset + 8 size) = num_slices * 20
    uint32_t num_slices  = (uint32_t)slice_meta.size();
    uint64_t dir_start   = 16 + meta_len;
    uint64_t data_start  = dir_start + num_slices * 20;

    // fill in absolute offsets
    uint64_t cursor = data_start;
    for (size_t i = 0; i < slice_meta.size(); i++) {
        slice_meta[i].offset = cursor;
        cursor += slice_meta[i].size;
    }

    // ── write file ────────────────────────────────────────────────────────

    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        std::cerr << "could not write " << output_path << "\n";
        return false;
    }

    uint8_t flags = 0;
    if (include_bytecode) flags |= 0x01;
    if (bytecode_only)    flags |= 0x02;

    // fixed header (16 bytes)
    out.write("JADE", 4);
    uint8_t version = 1;
    out.write((char*)&version,    1);
    out.write((char*)&flags,      1);
    uint16_t reserved = 0;
    out.write((char*)&reserved,   2);
    out.write((char*)&num_slices, 4);
    out.write((char*)&meta_len,   4);

    // metadata
    out.write(metadata.c_str(), meta_len);

    // slice directory
    for (auto& s : slice_meta) {
        out.write(s.tag,          4);
        out.write((char*)&s.offset, 8);
        out.write((char*)&s.size,   8);
    }

    // slice data
    for (auto& d : slice_data)
        out.write(d.data(), d.size());

    uint64_t total = cursor;
    std::cout << "wrote " << output_path << " ("
              << total << " bytes, "
              << num_slices << " slice(s): ";
    for (size_t i = 0; i < slice_meta.size(); i++) {
        if (i) std::cout << " + ";
        std::cout.write(slice_meta[i].tag, 4);
        std::cout << "(" << slice_meta[i].size << "b)";
    }
    std::cout << ")\n";
    return true;
}

} // namespace jscc
