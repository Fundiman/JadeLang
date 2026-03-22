#pragma once
#include "ast.hpp"
#include "error.hpp"
#include "sema.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/IR/LegacyPassManager.h>
#include <cstring>

#include <string>
#include <memory>
#include <unordered_map>

namespace jscc {

class Codegen {
public:
    Codegen(const std::string& module_name, ErrorReporter& reporter);

    // generate LLVM IR from AST
    bool generate(Program& program);

    // emit native object file (.o)
    bool emit_object(const std::string& output_path);

    // emit LLVM IR text (for debugging)
    void dump_ir();

    // emit .joc file — multi-arch with bytecode fallback
    bool emit_joc(const std::string& output_path,
                  const std::vector<std::string>& extra_targets = {},
                  bool include_bytecode = true,
                  bool bytecode_only    = false);

private:
    ErrorReporter&                    m_reporter;
    std::unique_ptr<llvm::LLVMContext> m_ctx;
    std::unique_ptr<llvm::Module>      m_module;
    std::unique_ptr<llvm::IRBuilder<>> m_builder;

    // current function being compiled
    llvm::Function*   m_current_fn = nullptr;

    // symbol table: name -> alloca (stack slot)
    std::unordered_map<std::string, llvm::Value*> m_locals;

    // ── type mapping ──────────────────────────────────────────────────────
    llvm::Type* llvm_type(const std::string& jade_type);
    llvm::Type* llvm_type_from_ref(const TypeRef& t);

    // ── codegen passes ────────────────────────────────────────────────────
    void        gen_stmt(Stmt* stmt);
    llvm::Value* gen_expr(Expr* expr);

    void        gen_def(Stmt* stmt);
    void        gen_val_var(Stmt* stmt);
    void        gen_return(Stmt* stmt);
    void        gen_if(Stmt* stmt);
    void        gen_while(Stmt* stmt);
    void        gen_for(Stmt* stmt);
    void        gen_block(std::vector<StmtPtr>& body);

    llvm::Value* gen_binary(Expr* expr);
    llvm::Value* gen_unary(Expr* expr);
    llvm::Value* gen_call(Expr* expr);
    llvm::Value* gen_method_call(Expr* expr);
    llvm::Value* gen_when(Expr* expr);

    // ── stdlib intrinsics ─────────────────────────────────────────────────
    void declare_stdlib_intrinsics();
    llvm::Function* get_or_declare(const std::string& name,
                                   llvm::FunctionType* ft);

    // ── helpers ───────────────────────────────────────────────────────────
    llvm::Value*    get_local(const std::string& name);
    void            set_local(const std::string& name, llvm::Value* val);
    llvm::AllocaInst* create_entry_alloca(llvm::Function* fn,
                                           const std::string& name,
                                           llvm::Type* type);
    llvm::Value*    i32(int v);
    llvm::Value*    i1(bool v);
    llvm::Value*    f64(double v);
};

} // namespace jscc
