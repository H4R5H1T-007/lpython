#include <iostream>
#include <map>
#include <memory>

#include <lfortran/ast.h>
#include <lfortran/asr.h>
#include <lfortran/pickle.h>
#include <lfortran/semantics/ast_to_asr.h>
#include <lfortran/parser/parser_stype.h>


namespace LFortran {

static inline ASR::expr_t* EXPR(const ASR::asr_t *f)
{
    LFORTRAN_ASSERT(f->type == ASR::asrType::expr);
    return (ASR::expr_t*)f;
}

static inline ASR::Variable_t* VARIABLE(const ASR::asr_t *f)
{
    LFORTRAN_ASSERT(f->type == ASR::asrType::var);
    ASR::var_t *t = (ASR::var_t *)f;
    LFORTRAN_ASSERT(t->type == ASR::varType::Variable);
    return (ASR::Variable_t*)t;
}

static inline ASR::Subroutine_t* SUBROUTINE(const ASR::asr_t *f)
{
    LFORTRAN_ASSERT(f->type == ASR::asrType::sub);
    ASR::sub_t *t = (ASR::sub_t *)f;
    LFORTRAN_ASSERT(t->type == ASR::subType::Subroutine);
    return (ASR::Subroutine_t*)t;
}

static inline ASR::TranslationUnit_t* TRANSLATION_UNIT(const ASR::asr_t *f)
{
    LFORTRAN_ASSERT(f->type == ASR::asrType::unit);
    ASR::unit_t *t = (ASR::unit_t *)f;
    LFORTRAN_ASSERT(t->type == ASR::unitType::TranslationUnit);
    return (ASR::TranslationUnit_t*)t;
}

static inline ASR::stmt_t* STMT(const ASR::asr_t *f)
{
    LFORTRAN_ASSERT(f->type == ASR::asrType::stmt);
    return (ASR::stmt_t*)f;
}

static inline ASR::ttype_t* TYPE(const ASR::asr_t *f)
{
    LFORTRAN_ASSERT(f->type == ASR::asrType::ttype);
    return (ASR::ttype_t*)f;
}

class SymbolTableVisitor : public AST::BaseWalkVisitor<SymbolTableVisitor>
{
public:
    ASR::asr_t *asr;
    Allocator &al;
    SymbolTable *translation_unit_scope;
    SymbolTable *subroutine_scope;

    SymbolTableVisitor(Allocator &al) : al{al} {
        translation_unit_scope = al.make_new<SymbolTable>();
        subroutine_scope = al.make_new<SymbolTable>();
    }

    void visit_TranslationUnit(const AST::TranslationUnit_t &x) {
        for (size_t i=0; i<x.n_items; i++) {
            visit_ast(*x.m_items[i]);
        }
        asr = ASR::make_TranslationUnit_t(al, x.base.base.loc,
            translation_unit_scope);
    }

    void visit_Subroutine(const AST::Subroutine_t &x) {
        for (size_t i=0; i<x.n_decl; i++) {
            visit_unit_decl2(*x.m_decl[i]);
        }
        // TODO: save the arguments into `a_args` and `n_args`.
        // We need to get Variables settled first, then it will be just a
        // reference to a variable.
        for (size_t i=0; i<x.n_args; i++) {
            char *arg=x.m_args[i].m_arg;
            std::string args = arg;
            if (subroutine_scope->scope.find(args) == subroutine_scope->scope.end()) {
                throw SemanticError("Dummy argument '" + args + "' not defined", x.base.base.loc);
            }
        }
        asr = ASR::make_Subroutine_t(
            al, x.base.base.loc,
            /* a_name */ x.m_name,
            /* a_args */ nullptr,
            /* n_args */ 0,
            /* a_body */ nullptr,
            /* n_body */ 0,
            /* a_bind */ nullptr,
            /* a_symtab */ subroutine_scope);
        std::string sym_name = x.m_name;
        if (translation_unit_scope->scope.find(sym_name) != translation_unit_scope->scope.end()) {
            throw SemanticError("Subroutine already defined", asr->loc);
        }
        translation_unit_scope->scope[sym_name] = asr;
    }

    void visit_decl(const AST::decl_t &x) {
        std::string sym = x.m_sym;
        std::string sym_type = x.m_sym_type;
        if (subroutine_scope->scope.find(sym) == subroutine_scope->scope.end()) {
            int s_type;
            if (sym_type == "integer") {
                s_type = 2;
            } else if (sym_type == "real") {
                s_type = 1;
            } else {
                Location loc;
                // TODO: decl_t does not have location information...
                loc.first_column = 0;
                loc.first_line = 0;
                loc.last_column = 0;
                loc.last_line = 0;
                throw SemanticError("Unsupported type", loc);
            }
            int s_intent=intent_local;
            if (x.n_attrs > 0) {
                AST::Attribute_t *a = (AST::Attribute_t*)(x.m_attrs[0]);
                if (std::string(a->m_name) == "intent") {
                    if (a->n_args > 0) {
                        std::string intent = std::string(a->m_args[0].m_arg);
                        if (intent == "in") {
                            s_intent = intent_in;
                        } else if (intent == "out") {
                            s_intent = intent_out;
                        } else if (intent == "inout") {
                            s_intent = intent_inout;
                        } else {
                            Location loc;
                            // TODO: decl_t does not have location information...
                            loc.first_column = 0;
                            loc.first_line = 0;
                            loc.last_column = 0;
                            loc.last_line = 0;
                            throw SemanticError("Incorrect intent specifier", loc);
                        }
                    } else {
                        Location loc;
                        // TODO: decl_t does not have location information...
                        loc.first_column = 0;
                        loc.first_line = 0;
                        loc.last_column = 0;
                        loc.last_line = 0;
                        throw SemanticError("intent() is empty. Must specify intent", loc);
                    }
                }
            }
            Location loc;
            // TODO: decl_t does not have location information...
            loc.first_column = 0;
            loc.first_line = 0;
            loc.last_column = 0;
            loc.last_line = 0;
            ASR::ttype_t *type;
            if (s_type == 1) {
                type = TYPE(ASR::make_Real_t(al, loc, 4, nullptr, 0));
            } else {
                LFORTRAN_ASSERT(s_type == 2);
                type = TYPE(ASR::make_Integer_t(al, loc, 4, nullptr, 0));
            }
            ASR::asr_t *v = ASR::make_Variable_t(al, loc, x.m_sym, s_intent, type);
            subroutine_scope->scope[sym] = v;

        }
    }

    void visit_Function(const AST::Function_t &x) {
        ASR::ttype_t *type = TYPE(ASR::make_Integer_t(al, x.base.base.loc,
                8, nullptr, 0));
        ASR::expr_t *return_var = EXPR(ASR::make_VariableOld_t(al, x.base.base.loc,
                x.m_name, nullptr, 1, type));
        asr = ASR::make_Function_t(al, x.base.base.loc,
            /*char* a_name*/ x.m_name,
            /*expr_t** a_args*/ nullptr, /*size_t n_args*/ 0,
            /*stmt_t** a_body*/ nullptr, /*size_t n_body*/ 0,
            /*tbind_t* a_bind*/ nullptr,
            /*expr_t* a_return_var*/ return_var,
            /*char* a_module*/ nullptr,
            /*int *object* a_symtab*/ 0);
    }
};

class BodyVisitor : public AST::BaseVisitor<BodyVisitor>
{
public:
    Allocator &al;
    ASR::asr_t *asr, *tmp;
    SymbolTable *current_scope;
    BodyVisitor(Allocator &al, ASR::asr_t *unit) : al{al}, asr{unit} {}

    void visit_TranslationUnit(const AST::TranslationUnit_t &x) {
        current_scope = TRANSLATION_UNIT(asr)->m_global_scope;
        for (size_t i=0; i<x.n_items; i++) {
            visit_ast(*x.m_items[i]);
        }
    }

    void visit_Subroutine(const AST::Subroutine_t &x) {
    // TODO: visit the body (which will call the visit_Assignment below)
    // and append it to the body of the subroutine.
    // Check all variables.
    // TODO: add SymbolTable::find_symbol(), which will automatically return
    // an error
        ASR::asr_t *t = current_scope->scope[std::string(x.m_name)];
        ASR::Subroutine_t *v = SUBROUTINE(t);
        current_scope = v->m_symtab;
        //current_scope = current_scope->scope[std::string(x.m_name)].second;
        Vec<ASR::stmt_t*> body;
        body.reserve(al, x.n_body);
        for (size_t i=0; i<x.n_body; i++) {
            this->visit_stmt(*x.m_body[i]);
            ASR::stmt_t *stmt = STMT(tmp);
            body.push_back(al, stmt);
        }
    }

    void visit_Function(const AST::Function_t &x) {
        Vec<ASR::stmt_t*> body;
        body.reserve(al, 8);
        for (size_t i=0; i<x.n_body; i++) {
            this->visit_stmt(*x.m_body[i]);
            ASR::stmt_t *stmt = STMT(tmp);
            body.push_back(al, stmt);
        }
        // TODO:
        // We must keep track of the current scope, lookup this function in the
        // scope as "_current_function" and attach the body to it. For now we
        // simply assume `asr` is this very function:
        ASR::Function_t *current_fn = (ASR::Function_t*)asr;
        current_fn->m_body = &body.p[0];
        current_fn->n_body = body.size();
    }

    void visit_Assignment(const AST::Assignment_t &x) {
        // TODO: assign this to the function's body in the ASR.
        this->visit_expr(*x.m_target);
        ASR::expr_t *target = EXPR(tmp);
        this->visit_expr(*x.m_value);
        ASR::expr_t *value = EXPR(tmp);
        tmp = ASR::make_Assignment_t(al, x.base.base.loc, target, value);
    }
    void visit_BinOp(const AST::BinOp_t &x) {
        this->visit_expr(*x.m_left);
        ASR::expr_t *left = EXPR(tmp);
        this->visit_expr(*x.m_right);
        ASR::expr_t *right = EXPR(tmp);
        ASR::operatorType op;
        switch (x.m_op) {
            case (AST::Add) :
                op = ASR::Add;
                break;
            case (AST::Sub) :
                op = ASR::Sub;
                break;
            case (AST::Mul) :
                op = ASR::Mul;
                break;
            case (AST::Div) :
                op = ASR::Div;
                break;
            case (AST::Pow) :
                op = ASR::Pow;
                break;
        }
        LFORTRAN_ASSERT(left->type == right->type);
        // TODO: For now assume reals:
        ASR::ttype_t *type = TYPE(ASR::make_Real_t(al, x.base.base.loc,
                4, nullptr, 0));
        tmp = ASR::make_BinOp_t(al, x.base.base.loc,
                left, op, right, type);
    }
    void visit_Name(const AST::Name_t &x) {
        SymbolTable *scope = current_scope;
        ASR::Variable_t *v = VARIABLE(scope->scope[std::string(x.m_id)]);
        ASR::var_t *var = (ASR::var_t*)v;
        ASR::asr_t *tmp2 = ASR::make_Var_t(al, x.base.base.loc, scope, var);
        tmp = tmp2;
        /*
        ASR::ttype_t *type = TYPE(ASR::make_Integer_t(al, x.base.base.loc,
                8, nullptr, 0));
        tmp = ASR::make_VariableOld_t(al, x.base.base.loc,
                x.m_id, nullptr, 1, type);
        */
    }
    void visit_Num(const AST::Num_t &x) {
        ASR::ttype_t *type = TYPE(ASR::make_Integer_t(al, x.base.base.loc,
                8, nullptr, 0));
        tmp = ASR::make_Num_t(al, x.base.base.loc, x.m_n, type);
    }
};

ASR::asr_t *ast_to_asr(Allocator &al, AST::TranslationUnit_t &ast)
{
    SymbolTableVisitor v(al);
    v.visit_TranslationUnit(ast);
    ASR::asr_t *unit = v.asr;

    BodyVisitor b(al, unit);
    b.visit_TranslationUnit(ast);
    return unit;
}

} // namespace LFortran
