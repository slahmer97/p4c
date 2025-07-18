/*
Copyright 2013-present Barefoot Networks, Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "toP4.h"

#include <sstream>
#include <string>

#include "frontends/common/parser_options.h"
#include "frontends/p4-14/fromv1.0/v1model.h"
#include "frontends/p4/getV1ModelVersion.h"
#include "frontends/parsers/p4/p4parser.hpp"
#include "ir/dump.h"

namespace P4 {

Visitor::profile_t ToP4::init_apply(const IR::Node *node) {
    LOG4("Program dump:" << std::endl << dumpToString(node));
    listTerminators_init_apply_size = listTerminators.size();
    vectorSeparator_init_apply_size = vectorSeparator.size();
    return Inspector::init_apply(node);
}

void ToP4::end_apply(const IR::Node *) {
    if (outStream != nullptr) {
        cstring result = builder.toString();
        *outStream << result.c_str();
        outStream->flush();
    }
    BUG_CHECK(listTerminators.size() == listTerminators_init_apply_size,
              "inconsistent listTerminators");
    BUG_CHECK(vectorSeparator.size() == vectorSeparator_init_apply_size,
              "inconsistent vectorSeparator");
}

std::optional<cstring> ToP4::ifSystemFile(const IR::Node *node) {
    if (!node->srcInfo.isValid() || noIncludes) {
        return std::nullopt;
    }
    auto sourceFile = node->srcInfo.getSourceFile();
    if (isSystemFile(sourceFile)) {
        return sourceFile;
    }
    return std::nullopt;
}

namespace {
class DumpIR : public Inspector {
    unsigned depth;
    std::stringstream str;

    DumpIR(unsigned depth, unsigned startDepth) : depth(depth) {
        for (unsigned i = 0; i < startDepth; i++) str << IndentCtl::indent;
        setName("DumpIR");
        visitDagOnce = false;
    }
    void display(const IR::Node *node) {
        str << IndentCtl::endl;
        if (node->is<IR::Member>()) {
            node->Node::dbprint(str);
            str << node->to<IR::Member>()->member;
        } else if (node->is<IR::Constant>()) {
            node->Node::dbprint(str);
            str << " " << node;
        } else if (node->is<IR::VectorBase>()) {
            node->Node::dbprint(str);
            str << ", size=" << node->to<IR::VectorBase>()->size();
        } else if (node->is<IR::Path>()) {
            node->dbprint(str);
        } else {
            node->Node::dbprint(str);
        }
    }

    bool goDeeper(const IR::Node *node) const {
        return node->is<IR::Expression>() || node->is<IR::Path>() || node->is<IR::Type>();
    }

    bool preorder(const IR::Node *node) override {
        if (depth == 0) return false;
        display(node);
        if (goDeeper(node))
            // increase depth limit for expressions.
            depth++;
        else
            depth--;
        str << IndentCtl::indent;
        return true;
    }
    void postorder(const IR::Node *node) override {
        if (goDeeper(node))
            depth--;
        else
            depth++;
        str << IndentCtl::unindent;
    }

 public:
    static std::string dump(const IR::Node *node, unsigned depth, unsigned startDepth) {
        DumpIR dumper(depth, startDepth);
        node->apply(dumper);
        auto result = dumper.str.str();
        return result;
    }
};
}  // namespace

unsigned ToP4::curDepth() const {
    unsigned result = 0;
    auto ctx = getContext();
    while (ctx != nullptr) {
        ctx = ctx->parent;
        result++;
    }
    return result;
}

void ToP4::dump(unsigned depth, const IR::Node *node, unsigned adjDepth) {
    if (!showIR) return;
    if (node == nullptr) node = getOriginal();

    auto str = DumpIR::dump(node, depth, adjDepth + curDepth());
    bool spc = builder.lastIsSpace();
    builder.commentStart();
    builder.append(str);
    builder.commentEnd();
    builder.newline();
    if (spc)
        // rather heuristic, but the output is very ugly anyway
        builder.emitIndent();
}

bool ToP4::preorder(const IR::P4Program *program) {
    std::set<cstring> includesEmitted;

    bool first = true;
    dump(2);
    for (auto a : program->objects) {
        // Check where this declaration originates
        auto sourceFileOpt = ifSystemFile(a);
        // Errors can come from multiple files
        if (!a->is<IR::Type_Error>() && sourceFileOpt.has_value()) {
            /* FIXME -- when including a user header file (sourceFile !=
             * mainFile), do we want to emit an #include of it or not?  Probably
             * not when translating from P4-14, as that would create a P4-16
             * file that tries to include a P4-14 header.  Unless we want to
             * allow converting headers independently (is that even possible?).
             * For now we ignore mainFile and don't emit #includes for any
             * non-system header */
            auto sourceFile = sourceFileOpt.value();
            if (includesEmitted.find(sourceFile) == includesEmitted.end()) {
                if (sourceFile.startsWith(p4includePath)) {
                    const char *p = sourceFile.c_str() + strlen(p4includePath);
                    if (*p == '/') p++;
                    // TODO: This is v1model-specific code. This should be not part of the core
                    // pass.
                    if (P4V1::V1Model::instance.file.name == p) {
                        P4V1::GetV1ModelVersion g;
                        program->apply(g);
                        builder.append("#define V1MODEL_VERSION ");
                        builder.append(g.version);
                        builder.newline();
                    }
                    builder.append("#include <");
                    builder.append(p);
                    builder.append(">");
                    builder.newline();
                } else {
                    builder.append("#include \"");
                    builder.append(sourceFile);
                    builder.append("\"");
                    builder.newline();
                }
                includesEmitted.emplace(sourceFile);
            }
            first = false;
            continue;
        }
        if (!first) builder.newline();
        first = false;
        visit(a);
    }
    if (!program->objects.empty()) builder.newline();
    return false;
}

bool ToP4::preorder(const IR::Type_Bits *t) {
    if (t->expression) {
        builder.append("bit<(");
        visit(t->expression);
        builder.append(")>");
    } else {
        builder.append(t->toString());
    }
    return false;
}

bool ToP4::preorder(const IR::Type_String *t) {
    builder.append(t->toString());
    return false;
}

bool ToP4::preorder(const IR::Type_InfInt *t) {
    builder.append(t->toString());
    return false;
}

bool ToP4::preorder(const IR::Type_Var *t) {
    builder.append(t->name);
    return false;
}

bool ToP4::preorder(const IR::Type_Unknown *) {
    BUG("Cannot emit code for an unknown type");
    // builder.append("*unknown type*");
    return false;
}

bool ToP4::preorder(const IR::Type_Dontcare *) {
    builder.append("_");
    return false;
}

bool ToP4::preorder(const IR::Type_Void *) {
    builder.append("void");
    return false;
}

bool ToP4::preorder(const IR::Type_Name *t) {
    visit(t->path);
    return false;
}

bool ToP4::preorder(const IR::Type_Array *t) {
    dump(2);
    visit(t->elementType);
    builder.append("[");
    visit(t->size);
    builder.append("]");
    return false;
}

bool ToP4::preorder(const IR::Type_Specialized *t) {
    dump(3);
    visit(t->baseType);
    builder.append("<");
    setVecSep(", ");
    visit(t->arguments);
    doneVec();
    builder.append(">");
    return false;
}

bool ToP4::preorder(const IR::Argument *arg) {
    dump(2);
    if (!arg->name.name.isNullOrEmpty()) {
        builder.append(arg->name.name);
        builder.append(" = ");
    }
    visit(arg->expression);
    return false;
}

bool ToP4::printAnnotations(const IR::IAnnotated *ann) {
    if (!ann->hasAnnotations()) return false;

    setVecSep(" ");
    visit(&ann->getAnnotations());
    doneVec();

    return true;
}

bool ToP4::preorder(const IR::Type_Typedef *t) {
    dump(2);
    if (printAnnotations(t)) builder.spc();
    builder.append("typedef ");
    visit(t->type);
    builder.spc();
    builder.append(t->name);
    builder.endOfStatement();
    return false;
}

bool ToP4::preorder(const IR::Type_Newtype *t) {
    dump(2);
    if (printAnnotations(t)) builder.spc();
    builder.append("type ");
    visit(t->type);
    builder.spc();
    builder.append(t->name);
    builder.endOfStatement();
    return false;
}

bool ToP4::preorder(const IR::Type_BaseList *t) {
    dump(3);
    builder.append("tuple<");
    bool first = true;
    for (auto a : t->components) {
        if (!first) builder.append(", ");
        first = false;
        auto p4type = a->getP4Type();
        CHECK_NULL(p4type);
        visit(p4type);
    }
    builder.append(">");
    return false;
}

bool ToP4::preorder(const IR::P4ValueSet *t) {
    dump(1);
    if (printAnnotations(t)) builder.spc();
    builder.append("value_set<");
    auto p4type = t->elementType->getP4Type();
    CHECK_NULL(p4type);
    visit(p4type);
    builder.append(">");
    builder.append("(");
    visit(t->size);
    builder.append(")");
    builder.spc();
    builder.append(t->name);
    builder.endOfStatement();
    return false;
}

bool ToP4::preorder(const IR::Type_Enum *t) {
    dump(1);
    if (printAnnotations(t)) builder.spc();
    builder.append("enum ");
    builder.append(t->name);
    builder.spc();
    builder.blockStart();
    bool first = true;
    for (auto a : *t->getDeclarations()) {
        dump(2, a->getNode(), 1);
        if (!first) builder.append(",\n");
        first = false;
        builder.emitIndent();
        builder.append(a->getName());
    }
    builder.newline();
    builder.blockEnd(true);
    return false;
}

bool ToP4::preorder(const IR::Type_SerEnum *t) {
    dump(1);
    if (printAnnotations(t)) builder.spc();
    builder.append("enum ");
    visit(t->type);
    builder.spc();
    builder.append(t->name);
    builder.spc();
    builder.blockStart();
    bool first = true;
    for (auto a : t->members) {
        dump(2, a->getNode(), 1);
        if (!first) builder.append(",\n");
        first = false;
        builder.emitIndent();
        builder.append(a->getName());
        builder.append(" = ");
        visit(a->value);
    }
    builder.newline();
    builder.blockEnd(true);
    return false;
}

bool ToP4::preorder(const IR::TypeParameters *t) {
    if (!t->empty()) {
        builder.append("<");
        bool first = true;
        bool decl = isDeclaration;
        isDeclaration = false;
        for (auto a : t->parameters) {
            if (!first) builder.append(", ");
            first = false;
            visit(a);
        }
        isDeclaration = decl;
        builder.append(">");
    }
    return false;
}

bool ToP4::preorder(const IR::Method *m) {
    dump(1);
    if (printAnnotations(m)) builder.spc();
    const Context *ctx = getContext();
    bool standaloneFunction = !ctx || !ctx->node->is<IR::Type_Extern>();
    // standalone function declaration: not in a Vector of methods
    if (standaloneFunction) builder.append("extern ");

    if (m->isAbstract) builder.append("abstract ");
    auto t = m->type;
    BUG_CHECK(t != nullptr, "Method %1% has no type", m);
    if (t->returnType != nullptr) {
        visit(t->returnType);
        builder.spc();
    }
    builder.append(m->name);
    visit(t->typeParameters);
    visit(t->parameters);
    if (standaloneFunction) builder.endOfStatement();
    return false;
}

bool ToP4::preorder(const IR::Function *function) {
    dump(1);
    if (printAnnotations(function)) builder.spc();
    auto t = function->type;
    BUG_CHECK(t != nullptr, "Function %1% has no type", function);
    if (t->returnType != nullptr) {
        visit(t->returnType);
        builder.spc();
    }
    builder.append(function->name);
    visit(t->typeParameters);
    visit(t->parameters);
    builder.spc();
    visit(function->body);
    return false;
}

bool ToP4::preorder(const IR::Type_Extern *t) {
    dump(2);
    if (isDeclaration) {
        if (printAnnotations(t)) builder.spc();
        builder.append("extern ");
    }
    builder.append(t->name);
    visit(t->typeParameters);
    if (!isDeclaration) return false;
    builder.spc();
    builder.blockStart();

    if (t->attributes.size() != 0)
        warn(ErrorType::WARN_UNSUPPORTED,
             "%1%: extern has attributes, which are not supported "
             "in P4-16, and thus are not emitted as P4-16",
             t);

    setVecSep(";\n", ";\n");
    bool decl = isDeclaration;
    isDeclaration = true;
    preorder(&t->methods);
    isDeclaration = decl;
    doneVec();
    builder.blockEnd(true);
    return false;
}

bool ToP4::preorder(const IR::Type_Boolean *) {
    builder.append("bool");
    return false;
}

bool ToP4::preorder(const IR::Type_Varbits *t) {
    if (t->expression) {
        builder.append("varbit<(");
        visit(t->expression);
        builder.append(")>");
    } else {
        builder.appendFormat("varbit<%d>", t->size);
    }
    return false;
}

bool ToP4::preorder(const IR::Type_Package *package) {
    dump(2);
    builder.emitIndent();
    if (printAnnotations(package)) builder.spc();
    builder.append("package ");
    builder.append(package->name);
    visit(package->typeParameters);
    visit(package->constructorParams);
    if (isDeclaration) builder.endOfStatement();
    return false;
}

bool ToP4::process(const IR::Type_StructLike *t, const char *name) {
    dump(2);
    if (isDeclaration) {
        builder.emitIndent();
        if (printAnnotations(t)) builder.spc();
        builder.append(name);
        builder.spc();
    }
    builder.append(t->name);
    visit(t->typeParameters);
    if (!isDeclaration) return false;
    builder.spc();
    builder.blockStart();

    std::map<const IR::StructField *, cstring> type;
    size_t len = 0;
    for (auto f : t->fields) {
        Util::SourceCodeBuilder builder;
        ToP4 rec(builder, showIR);
        rec.isDeclaration = false;

        f->type->apply(rec);
        cstring t = builder.toString();
        if (t.size() > len) len = t.size();
        type.emplace(f, t);
    }

    for (auto f : t->fields) {
        dump(4, f, 1);  // this will dump annotations
        if (f->hasAnnotations()) {
            builder.emitIndent();
            if (printAnnotations(f)) builder.newline();
        }
        builder.emitIndent();
        cstring t = get(type, f);
        builder.append(t);
        size_t spaces = len + 1 - t.size();
        builder.append(std::string(spaces, ' '));
        builder.append(f->name);
        builder.endOfStatement(true);
    }

    builder.blockEnd(true);
    return false;
}

bool ToP4::preorder(const IR::Type_Parser *t) {
    dump(2);
    builder.emitIndent();
    if (printAnnotations(t)) builder.spc();
    builder.append("parser ");
    builder.append(t->name);
    visit(t->typeParameters);
    visit(t->applyParams);
    if (isDeclaration) builder.endOfStatement();
    return false;
}

bool ToP4::preorder(const IR::Type_Control *t) {
    dump(2);
    builder.emitIndent();
    if (printAnnotations(t)) builder.spc();
    builder.append("control ");
    builder.append(t->name);
    visit(t->typeParameters);
    visit(t->applyParams);
    if (isDeclaration) builder.endOfStatement();
    return false;
}

///////////////////////

bool ToP4::preorder(const IR::Constant *c) {
    const IR::Type_Bits *tb = c->type->to<IR::Type_Bits>();
    unsigned width;
    bool sign;
    if (tb == nullptr) {
        width = 0;
        sign = false;
    } else {
        width = tb->size;
        sign = tb->isSigned;
    }
    cstring s = Util::toString(c->value, width, sign, c->base);
    builder.append(s);
    return false;
}

bool ToP4::preorder(const IR::BoolLiteral *b) {
    builder.append(b->toString());
    return false;
}

bool ToP4::preorder(const IR::StringLiteral *s) {
    builder.append(s->toString());
    return false;
}

bool ToP4::preorder(const IR::Declaration_Constant *cst) {
    dump(2);
    if (printAnnotations(cst)) builder.spc();
    builder.append("const ");
    auto type = cst->type->getP4Type();
    CHECK_NULL(type);
    visit(type);
    builder.spc();
    builder.append(cst->name);
    builder.append(" = ");

    setListTerm("{ ", " }");
    visit(cst->initializer);
    doneList();

    builder.endOfStatement();
    return false;
}

bool ToP4::preorder(const IR::Declaration_Instance *i) {
    dump(3);
    if (printAnnotations(i)) builder.spc();
    auto type = i->type->getP4Type();
    CHECK_NULL(type);
    visit(type);
    builder.append("(");
    setVecSep(", ");
    visit(i->arguments);
    doneVec();
    builder.append(")");
    builder.spc();
    builder.append(i->name);
    if (i->initializer != nullptr) {
        builder.append(" = ");
        visit(i->initializer);
    }
    builder.endOfStatement();
    return false;
}

bool ToP4::preorder(const IR::Declaration_Variable *v) {
    dump(2);
    if (printAnnotations(v)) builder.spc();
    auto type = v->type->getP4Type();
    CHECK_NULL(type);
    visit(type);
    builder.spc();
    builder.append(v->name);
    if (v->initializer != nullptr) {
        builder.append(" = ");
        setListTerm("{ ", " }");
        visit(v->initializer);
        doneList();
    }
    builder.endOfStatement();
    return false;
}

bool ToP4::preorder(const IR::Type_Error *d) {
    dump(1);

    const auto userErrors = d->getDeclarations()
                                ->where([this](const IR::IDeclaration *e) {
                                    // only print if not from a system file
                                    return !ifSystemFile(e->getNode()).has_value();
                                })
                                ->toVector();
    if (!isDeclaration || !userErrors.empty()) {
        builder.append("error ");

        if (!isDeclaration) {
            return false;
        }

        bool first = true;
        builder.blockStart();
        for (auto a : userErrors) {
            if (!first) {
                builder.append(",\n");
            }
            dump(1, a->getNode(), 1);
            first = false;
            builder.emitIndent();
            builder.append(a->getName());
        }
        builder.newline();
        builder.blockEnd(true);
    }
    return false;
}

bool ToP4::preorder(const IR::Declaration_MatchKind *d) {
    dump(1);
    builder.append("match_kind ");
    builder.blockStart();
    bool first = true;
    for (auto a : *d->getDeclarations()) {
        if (!first) builder.append(",\n");
        dump(1, a->getNode(), 1);
        first = false;
        builder.emitIndent();
        builder.append(a->getName());
    }
    builder.newline();
    builder.blockEnd(true);
    return false;
}

///////////////////////////////////////////////////

#define VECTOR_VISIT(V, T)                                    \
    bool ToP4::preorder(const IR::V<IR::T> *v) {              \
        if (v == nullptr) return false;                       \
        bool first = true;                                    \
        VecPrint sep = getSep();                              \
        for (auto a : *v) {                                   \
            if (!first) {                                     \
                builder.append(sep.separator);                \
            }                                                 \
            if (sep.separator.endsWith("\n")) {               \
                builder.emitIndent();                         \
            }                                                 \
            first = false;                                    \
            visit(a);                                         \
        }                                                     \
        if (!v->empty() && !sep.terminator.isNullOrEmpty()) { \
            builder.append(sep.terminator);                   \
        }                                                     \
        return false;                                         \
    }

VECTOR_VISIT(Vector, ActionListElement)
VECTOR_VISIT(Vector, Annotation)
VECTOR_VISIT(Vector, Entry)
VECTOR_VISIT(Vector, Expression)
VECTOR_VISIT(Vector, Argument)
VECTOR_VISIT(Vector, KeyElement)
VECTOR_VISIT(Vector, Method)
VECTOR_VISIT(Vector, Node)
VECTOR_VISIT(Vector, SelectCase)
VECTOR_VISIT(Vector, SwitchCase)
VECTOR_VISIT(Vector, Type)
VECTOR_VISIT(IndexedVector, Declaration)
VECTOR_VISIT(IndexedVector, Declaration_ID)
VECTOR_VISIT(IndexedVector, Node)
VECTOR_VISIT(IndexedVector, ParserState)
VECTOR_VISIT(IndexedVector, StatOrDecl)

#undef VECTOR_VISIT

///////////////////////////////////////////

bool ToP4::preorder(const IR::AbstractSlice *slice) {
    int prec = expressionPrecedence;
    bool useParens = prec > slice->getPrecedence();
    if (useParens) builder.append("(");
    expressionPrecedence = slice->getPrecedence();

    visit(slice->e0);
    builder.append("[");
    expressionPrecedence = DBPrint::Prec_Low;
    visit(slice->e1);
    if (slice->is<IR::PlusSlice>()) builder.append("+");
    builder.append(":");
    expressionPrecedence = DBPrint::Prec_Low;
    visit(slice->e2);
    builder.append("]");
    expressionPrecedence = prec;

    if (useParens) builder.append(")");
    return false;
}

bool ToP4::preorder(const IR::DefaultExpression *) {
    // Within a method call this is rendered as a don't care
    if (withinArgument)
        builder.append("_");
    else
        builder.append("default");
    return false;
}

bool ToP4::preorder(const IR::This *) {
    builder.append("this");
    return false;
}

bool ToP4::preorder(const IR::PathExpression *p) {
    visit(p->path);
    return false;
}

bool ToP4::preorder(const IR::TypeNameExpression *e) {
    visit(e->typeName);
    return false;
}

bool ToP4::preorder(const IR::ConstructorCallExpression *e) {
    visit(e->constructedType);
    builder.append("(");
    setVecSep(", ");
    int prec = expressionPrecedence;
    expressionPrecedence = DBPrint::Prec_Low;
    visit(e->arguments);
    expressionPrecedence = prec;
    doneVec();
    builder.append(")");
    return false;
}

bool ToP4::preorder(const IR::Member *e) {
    int prec = expressionPrecedence;
    expressionPrecedence = e->getPrecedence();
    visit(e->expr);
    builder.append(".");
    builder.append(e->member);
    expressionPrecedence = prec;
    return false;
}

bool ToP4::preorder(const IR::SelectCase *e) {
    dump(2);
    int prec = expressionPrecedence;
    expressionPrecedence = DBPrint::Prec_Low;
    setListTerm("(", ")");
    visit(e->keyset);
    expressionPrecedence = prec;
    doneList();
    builder.append(": ");
    visit(e->state);
    return false;
}

bool ToP4::preorder(const IR::SelectExpression *e) {
    builder.append("select(");
    int prec = expressionPrecedence;
    expressionPrecedence = DBPrint::Prec_Low;
    setListTerm("", "");
    visit(e->select);
    doneList();
    builder.append(") ");
    builder.blockStart();
    setVecSep(";\n", ";\n");
    expressionPrecedence = DBPrint::Prec_Low;
    preorder(&e->selectCases);
    doneVec();
    builder.blockEnd(true);
    expressionPrecedence = prec;
    return false;
}

bool ToP4::preorder(const IR::ListExpression *e) {
    cstring start, end;
    if (listTerminators.empty()) {
        start = "{ "_cs;
        end = " }"_cs;
    } else {
        start = listTerminators.back().start;
        end = listTerminators.back().end;
    }
    builder.append(start);
    setVecSep(", ");
    int prec = expressionPrecedence;
    expressionPrecedence = DBPrint::Prec_Low;
    setListTerm("{ ", " }");
    preorder(&e->components);
    doneList();
    expressionPrecedence = prec;
    doneVec();
    builder.append(end);
    return false;
}

bool ToP4::preorder(const IR::P4ListExpression *e) {
    if (expressionPrecedence > DBPrint::Prec_Prefix) builder.append("(");
    if (e->elementType != nullptr) {
        builder.append("(list<");
        visit(e->elementType->getP4Type());
        builder.append(">)");
    }
    builder.append("{");
    int prec = expressionPrecedence;
    expressionPrecedence = DBPrint::Prec_Low;
    bool first = true;
    for (auto c : e->components) {
        if (!first) builder.append(",");
        first = false;
        visit(c);
    }
    expressionPrecedence = prec;
    builder.append("}");
    if (expressionPrecedence > DBPrint::Prec_Prefix) builder.append(")");
    return false;
}

bool ToP4::preorder(const IR::NamedExpression *e) {
    builder.append(e->name.name);
    builder.append(" = ");
    visit(e->expression);
    return false;
}

bool ToP4::preorder(const IR::StructExpression *e) {
    if (expressionPrecedence > DBPrint::Prec_Prefix) builder.append("(");
    if (e->structType != nullptr) {
        builder.append("(");
        visit(e->structType);
        builder.append(")");
    }
    builder.append("{");
    int prec = expressionPrecedence;
    expressionPrecedence = DBPrint::Prec_Low;
    bool first = true;
    for (auto c : e->components) {
        if (!first) builder.append(",");
        first = false;
        visit(c);
    }
    expressionPrecedence = prec;
    builder.append("}");
    if (expressionPrecedence > DBPrint::Prec_Prefix) builder.append(")");
    return false;
}

bool ToP4::preorder(const IR::HeaderStackExpression *e) {
    if (expressionPrecedence > DBPrint::Prec_Prefix) builder.append("(");
    if (e->headerStackType != nullptr) {
        builder.append("(");
        visit(e->headerStackType);
        builder.append(")");
    }
    builder.append("{");
    int prec = expressionPrecedence;
    expressionPrecedence = DBPrint::Prec_Low;
    bool first = true;
    for (auto c : e->components) {
        if (!first) builder.append(",");
        first = false;
        visit(c);
    }
    expressionPrecedence = prec;
    builder.append("}");
    if (expressionPrecedence > DBPrint::Prec_Prefix) builder.append(")");
    return false;
}

bool ToP4::preorder(const IR::Invalid *) {
    builder.append("{#}");
    return false;
}

bool ToP4::preorder(const IR::Dots *) {
    builder.append("...");
    return false;
}

bool ToP4::preorder(const IR::NamedDots *) {
    builder.append("...");
    return false;
}

bool ToP4::preorder(const IR::InvalidHeader *e) {
    if (expressionPrecedence > DBPrint::Prec_Prefix) builder.append("(");
    builder.append("(");
    visit(e->headerType);
    builder.append(")");
    builder.append("{#}");
    if (expressionPrecedence > DBPrint::Prec_Prefix) builder.append(")");
    return false;
}

bool ToP4::preorder(const IR::InvalidHeaderUnion *e) {
    if (expressionPrecedence > DBPrint::Prec_Prefix) builder.append("(");
    builder.append("(");
    visit(e->headerUnionType);
    builder.append(")");
    builder.append("{#}");
    if (expressionPrecedence > DBPrint::Prec_Prefix) builder.append(")");
    return false;
}

bool ToP4::preorder(const IR::MethodCallExpression *e) {
    int prec = expressionPrecedence;
    bool useParens = (prec > DBPrint::Prec_Postfix) ||
                     (!e->typeArguments->empty() && prec >= DBPrint::Prec_Cond);
    // FIXME: we use parenthesis more often than necessary
    // because the bison parser has a bug which parses
    // these expressions incorrectly.
    expressionPrecedence = DBPrint::Prec_Postfix;
    if (useParens) builder.append("(");
    visit(e->method);
    if (!e->typeArguments->empty()) {
        bool decl = isDeclaration;
        isDeclaration = false;
        builder.append("<");
        setVecSep(", ");
        visit(e->typeArguments);
        doneVec();
        builder.append(">");
        isDeclaration = decl;
    }
    builder.append("(");
    setVecSep(", ");
    expressionPrecedence = DBPrint::Prec_Low;
    withinArgument = true;
    visit(e->arguments);
    withinArgument = false;
    doneVec();
    builder.append(")");
    if (useParens) builder.append(")");
    expressionPrecedence = prec;
    return false;
}

bool ToP4::preorder(const IR::Operation_Binary *b) {
    int prec = expressionPrecedence;
    bool useParens = prec > b->getPrecedence();
    if (useParens) builder.append("(");
    expressionPrecedence = b->getPrecedence();
    visit(b->left);
    builder.spc();
    builder.append(b->getStringOp());
    builder.spc();
    expressionPrecedence = b->getPrecedence() + 1;
    visit(b->right);
    if (useParens) builder.append(")");
    expressionPrecedence = prec;
    return false;
}

bool ToP4::preorder(const IR::Mux *b) {
    int prec = expressionPrecedence;
    bool useParens = prec >= b->getPrecedence();
    if (useParens) builder.append("(");
    expressionPrecedence = b->getPrecedence();
    visit(b->e0);
    builder.append(" ? ");
    expressionPrecedence = DBPrint::Prec_Low;
    visit(b->e1);
    builder.append(" : ");
    expressionPrecedence = b->getPrecedence();
    visit(b->e2);
    expressionPrecedence = prec;
    if (useParens) builder.append(")");
    return false;
}

bool ToP4::preorder(const IR::Operation_Unary *u) {
    int prec = expressionPrecedence;
    bool useParens = prec > u->getPrecedence();
    if (useParens) builder.append("(");
    builder.append(u->getStringOp());
    expressionPrecedence = u->getPrecedence();
    visit(u->expr);
    expressionPrecedence = prec;
    if (useParens) builder.append(")");
    return false;
}

bool ToP4::preorder(const IR::ArrayIndex *a) {
    int prec = expressionPrecedence;
    bool useParens = prec > a->getPrecedence();
    if (useParens) builder.append("(");
    expressionPrecedence = a->getPrecedence();
    visit(a->left);
    builder.append("[");
    expressionPrecedence = DBPrint::Prec_Low;
    visit(a->right);
    builder.append("]");
    if (useParens) builder.append(")");
    expressionPrecedence = prec;
    return false;
}

bool ToP4::preorder(const IR::Cast *c) {
    int prec = expressionPrecedence;
    bool useParens = prec > c->getPrecedence();
    if (useParens) builder.append("(");
    builder.append("(");
    visit(c->destType);
    builder.append(")");
    expressionPrecedence = c->getPrecedence();
    visit(c->expr);
    if (useParens) builder.append(")");
    expressionPrecedence = prec;
    return false;
}

//////////////////////////////////////////////////////////

bool ToP4::preorder(const IR::AssignmentStatement *a) {
    dump(2);
    visit(a->left);
    builder.append(" = ");
    visit(a->right);
    builder.endOfStatement();
    return false;
}

bool ToP4::preorder(const IR::OpAssignmentStatement *a) {
    dump(2);
    visit(a->left);
    builder.append(" ");
    builder.append(a->getStringOp());
    builder.append("= ");
    visit(a->right);
    builder.endOfStatement();
    return false;
}

bool ToP4::preorder(const IR::BlockStatement *s) {
    dump(1);
    if (printAnnotations(s)) builder.spc();
    builder.blockStart();
    setVecSep("\n", "\n");
    preorder(&s->components);
    doneVec();
    builder.blockEnd(false);
    return false;
}

bool ToP4::preorder(const IR::BreakStatement *) {
    dump(1);
    builder.append("break");
    builder.endOfStatement();
    return false;
}

bool ToP4::preorder(const IR::ContinueStatement *) {
    dump(1);
    builder.append("continue");
    builder.endOfStatement();
    return false;
}

bool ToP4::preorder(const IR::ExitStatement *) {
    dump(1);
    builder.append("exit");
    builder.endOfStatement();
    return false;
}

bool ToP4::preorder(const IR::ReturnStatement *statement) {
    dump(2);
    builder.append("return");
    if (statement->expression != nullptr) {
        builder.spc();
        visit(statement->expression);
    }
    builder.endOfStatement();
    return false;
}

bool ToP4::preorder(const IR::EmptyStatement *) {
    dump(1);
    builder.endOfStatement();
    return false;
}

bool ToP4::preorder(const IR::IfStatement *s) {
    dump(2);
    builder.append("if (");
    visit(s->condition);
    builder.append(") ");
    if (!s->ifTrue->is<IR::BlockStatement>()) {
        builder.append("{");
        builder.increaseIndent();
        builder.newline();
        builder.emitIndent();
    }
    visit(s->ifTrue);
    if (!s->ifTrue->is<IR::BlockStatement>()) {
        builder.newline();
        builder.decreaseIndent();
        builder.emitIndent();
        builder.append("}");
    }
    if (s->ifFalse != nullptr) {
        builder.append(" else ");
        if (!s->ifFalse->is<IR::BlockStatement>() && !s->ifFalse->is<IR::IfStatement>()) {
            builder.append("{");
            builder.increaseIndent();
            builder.newline();
            builder.emitIndent();
        }
        visit(s->ifFalse);
        if (!s->ifFalse->is<IR::BlockStatement>() && !s->ifFalse->is<IR::IfStatement>()) {
            builder.newline();
            builder.decreaseIndent();
            builder.emitIndent();
            builder.append("}");
        }
    }
    return false;
}

bool ToP4::preorder(const IR::ForStatement *s) {
    dump(2);
    if (printAnnotations(s)) builder.spc();
    builder.append("for (");
    bool first = true;
    for (auto *d : s->init) {
        if (!first) builder.append(", ");
        builder.supressStatementSemi();
        visit(d, "init");
        first = false;
    }
    builder.append("; ");
    visit(s->condition, "condition");
    builder.append("; ");
    first = true;
    for (auto *e : s->updates) {
        if (e->is<IR::EmptyStatement>()) continue;
        if (!first) builder.append(", ");
        builder.supressStatementSemi();
        visit(e, "updates");
        first = false;
    }
    builder.append(") ");
    if (!s->body->is<IR::BlockStatement>()) {
        builder.append("{");
        builder.increaseIndent();
        builder.newline();
        builder.emitIndent();
    }
    visit(s->body, "body");
    if (!s->body->is<IR::BlockStatement>()) {
        builder.newline();
        builder.decreaseIndent();
        builder.emitIndent();
        builder.append("}");
    }
    return false;
}

bool ToP4::preorder(const IR::ForInStatement *s) {
    dump(2);
    if (printAnnotations(s)) builder.spc();
    builder.append("for (");
    if (s->decl) {
        builder.supressStatementSemi();
        visit(s->decl, "decl");
    } else {
        auto *decl = resolveUnique(s->ref->path->name, ResolutionType::Any);
        if (auto *di = decl->to<IR::Declaration_Variable>()) {
            builder.supressStatementSemi();
            visit(di, "decl");
        } else {
            visit(s->ref, "ref");
        }
    }
    builder.append(" in ");
    visit(s->collection);
    builder.append(") ");
    if (!s->body->is<IR::BlockStatement>()) {
        builder.append("{");
        builder.increaseIndent();
        builder.newline();
        builder.emitIndent();
    }
    visit(s->body, "body");
    if (!s->body->is<IR::BlockStatement>()) {
        builder.newline();
        builder.decreaseIndent();
        builder.emitIndent();
        builder.append("}");
    }
    return false;
}

bool ToP4::preorder(const IR::MethodCallStatement *s) {
    dump(3);
    visit(s->methodCall);
    builder.endOfStatement();
    return false;
}

bool ToP4::preorder(const IR::SwitchCase *s) {
    visit(s->label);
    builder.append(": ");
    visit(s->statement);
    return false;
}

bool ToP4::preorder(const IR::SwitchStatement *s) {
    dump(4);
    builder.append("switch (");
    visit(s->expression);
    builder.append(") ");
    builder.blockStart();
    setVecSep("\n", "\n");
    preorder(&s->cases);
    doneVec();
    builder.blockEnd(false);
    return false;
}

////////////////////////////////////

bool ToP4::preorder(const IR::Annotation *a) {
    builder.append("@");
    builder.append(a->name);
    const char *open = a->structured ? "[" : "(";
    const char *close = a->structured ? "]" : ")";

    std::visit(
        [&](const auto &body) {
            using T = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<T, IR::Vector<IR::AnnotationToken>>) {
                // Do not print () for empty unstructured annotations
                if (body.empty()) return;
                // Have an unparsed annotation.
                // We could be prettier here with smarter logic, but let's do the easy
                // thing by separating every token with a space.
                builder.append(open);
                const char *sep = "";
                for (auto tok : body) {
                    builder.append(sep);
                    sep = " ";

                    bool haveStringLiteral =
                        tok->token_type == P4Parser::token_type::TOK_STRING_LITERAL;
                    if (haveStringLiteral) builder.append("\"");
                    builder.append(tok->text);
                    if (haveStringLiteral) builder.append("\"");
                }
                builder.append(close);
            } else if constexpr (std::is_same_v<T, IR::Vector<IR::Expression>>) {
                // Do not print () for empty unstructured annotations
                if (body.empty() && !a->structured) return;
                builder.append(open);
                setVecSep(", ");
                preorder(&body);
                doneVec();
                builder.append(close);
            } else if constexpr (std::is_same_v<T, IR::IndexedVector<IR::NamedExpression>>) {
                if (body.empty() && !a->structured) return;
                builder.append(open);
                bool first = true;
                for (auto kvp : body) {
                    if (!first) builder.append(", ");
                    first = false;
                    builder.append(kvp->name);
                    builder.append("=");
                    visit(kvp->expression);
                }
                builder.append(close);
            } else {
                BUG("Unexpected variant field");
            }
        },
        a->body);

    return false;
}

bool ToP4::preorder(const IR::Parameter *p) {
    dump(2);
    if (printAnnotations(p)) builder.spc();
    switch (p->direction) {
        case IR::Direction::None:
            break;
        case IR::Direction::In:
            builder.append("in ");
            break;
        case IR::Direction::Out:
            builder.append("out ");
            break;
        case IR::Direction::InOut:
            builder.append("inout ");
            break;
        default:
            BUG("Unexpected case");
    }
    bool decl = isDeclaration;
    isDeclaration = false;
    visit(p->type);
    isDeclaration = decl;
    builder.spc();
    builder.append(p->name);
    if (p->defaultValue != nullptr) {
        builder.append("=");
        visit(p->defaultValue);
    }
    return false;
}

bool ToP4::preorder(const IR::P4Control *c) {
    dump(1);
    bool decl = isDeclaration;
    isDeclaration = false;
    visit(c->type);
    isDeclaration = decl;
    if (c->constructorParams->size() != 0) visit(c->constructorParams);
    builder.spc();
    builder.blockStart();
    for (auto s : c->controlLocals) {
        builder.emitIndent();
        visit(s);
        builder.newline();
    }

    builder.emitIndent();
    builder.append("apply ");
    visit(c->body);
    builder.newline();
    builder.blockEnd(true);
    return false;
}

bool ToP4::preorder(const IR::ParameterList *p) {
    builder.append("(");
    bool first = true;
    for (auto param : *p->getEnumerator()) {
        if (!first) builder.append(", ");
        first = false;
        visit(param);
    }
    builder.append(")");
    return false;
}

bool ToP4::preorder(const IR::P4Action *c) {
    dump(2);
    if (printAnnotations(c)) builder.spc();
    builder.append("action ");
    builder.append(c->name);
    visit(c->parameters);
    builder.spc();
    visit(c->body);
    return false;
}

bool ToP4::preorder(const IR::ParserState *s) {
    dump(1);
    if (s->isBuiltin()) return false;

    if (printAnnotations(s)) builder.spc();
    builder.append("state ");
    builder.append(s->name);
    builder.spc();
    builder.blockStart();
    setVecSep("\n", "\n");
    preorder(&s->components);
    doneVec();

    if (s->selectExpression != nullptr) {
        dump(2, s->selectExpression, 1);
        builder.emitIndent();
        builder.append("transition ");
        visit(s->selectExpression);
        if (!s->selectExpression->is<IR::SelectExpression>()) {
            builder.endOfStatement();
            builder.newline();
        }
    }
    builder.blockEnd(false);
    return false;
}

bool ToP4::preorder(const IR::P4Parser *c) {
    dump(1);
    bool decl = isDeclaration;
    isDeclaration = false;
    visit(c->type);
    isDeclaration = decl;
    if (c->constructorParams->size() != 0) visit(c->constructorParams);
    builder.spc();
    builder.blockStart();
    setVecSep("\n", "\n");
    preorder(&c->parserLocals);
    doneVec();
    // explicit visit of parser states
    for (auto s : c->states) {
        if (s->isBuiltin()) continue;
        builder.emitIndent();
        visit(s);
        builder.append("\n");
    }
    builder.blockEnd(true);
    return false;
}

bool ToP4::preorder(const IR::ExpressionValue *v) {
    dump(2);
    visit(v->expression);
    builder.endOfStatement();
    return false;
}

bool ToP4::preorder(const IR::ActionListElement *ale) {
    dump(3);
    if (printAnnotations(ale)) builder.spc();
    visit(ale->expression);
    return false;
}

bool ToP4::preorder(const IR::ActionList *v) {
    dump(2);
    builder.blockStart();
    setVecSep(";\n", ";\n");
    preorder(&v->actionList);
    doneVec();
    builder.blockEnd(false);
    return false;
}

bool ToP4::preorder(const IR::Key *v) {
    dump(2);
    builder.blockStart();

    std::map<const IR::KeyElement *, cstring> kf;
    size_t len = 0;
    for (auto f : v->keyElements) {
        Util::SourceCodeBuilder builder;
        ToP4 rec(builder, showIR);

        f->expression->apply(rec);
        cstring s = builder.toString();
        if (s.size() > len) len = s.size();
        kf.emplace(f, s);
    }

    for (auto f : v->keyElements) {
        dump(2, f, 2);
        builder.emitIndent();
        cstring s = get(kf, f);
        builder.append(s);
        size_t spaces = len - s.size();
        builder.append(std::string(spaces, ' '));
        builder.append(": ");
        visit(f->matchType);
        if (f->hasAnnotations()) {
            builder.spc();
            printAnnotations(f);
        }
        builder.endOfStatement(true);
    }
    builder.blockEnd(false);
    return false;
}

bool ToP4::preorder(const IR::Property *p) {
    dump(1);
    if (printAnnotations(p)) builder.spc();
    if (p->isConstant) builder.append("const ");
    builder.append(p->name);
    builder.append(" = ");
    visit(p->value);
    return false;
}

bool ToP4::preorder(const IR::TableProperties *t) {
    for (auto p : t->properties) {
        builder.emitIndent();
        visit(p);
        builder.newline();
    }
    return false;
}

bool ToP4::preorder(const IR::EntriesList *l) {
    dump(1);
    builder.append("{");
    builder.newline();
    builder.increaseIndent();
    visit(&l->entries);
    builder.decreaseIndent();
    builder.emitIndent();
    builder.append("}");
    return false;
}

bool ToP4::preorder(const IR::Entry *e) {
    dump(2);
    builder.emitIndent();
    if (e->isConst) builder.append("const ");
    if (e->priority) {
        builder.append("priority=");
        visit(e->priority);
        builder.append(": ");
    }
    if (e->keys->components.size() == 1)
        setListTerm("", "");
    else
        setListTerm("(", ")");
    visit(e->keys);
    doneList();
    builder.append(" : ");
    visit(e->action);
    if (printAnnotations(e)) builder.spc();
    builder.append(";");
    return false;
}

bool ToP4::preorder(const IR::P4Table *c) {
    dump(2);
    if (printAnnotations(c)) builder.spc();
    builder.append("table ");
    builder.append(c->name);
    builder.spc();
    builder.blockStart();
    setVecSep("\n", "\n");
    visit(c->properties);
    doneVec();
    builder.blockEnd(false);
    return false;
}

bool ToP4::preorder(const IR::Path *p) {
    builder.append(p->asString());
    return false;
}

std::string toP4(const IR::INode *node) {
    std::stringstream stream;
    P4::ToP4 toP4(&stream, false);
    node->getNode()->apply(toP4);
    return stream.str();
}

void dumpP4(const IR::INode *node) {
    auto s = toP4(node);
    std::cout << s;
}

}  // namespace P4
