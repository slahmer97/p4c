
#include "opendesc.h"

#include <fstream>
#include <iostream>

#include "Analyzer.h"
#include "CompletionAnalyzer.h"
#include "ControlFlowGraph.h"
#include "RssHash.h"
#include "SoftwareCostModel.h"
#include "TopLevelTypeRegistrar.h"
#include "backends/p4test/version.h"
#include "control-plane/p4RuntimeSerializer.h"
#include "frontends/common/applyOptionsPragmas.h"
#include "frontends/common/parseInput.h"
#include "frontends/p4/evaluator/evaluator.h"
#include "frontends/p4/frontend.h"
#include "frontends/p4/toP4/toP4.h"
#include "ir/ir.h"
#include "ir/json_loader.h"
#include "ir/pass_utils.h"
#include "lib/crash.h"
#include "lib/error.h"
#include "lib/exceptions.h"
#include "lib/gc.h"
#include "lib/log.h"
#include "lib/nullstream.h"
#include "midend.h"

using namespace std;

OpenDescOptions::OpenDescOptions() {
    registerOption(
        "--parse-only", nullptr,
        [this](const char *) {
            parseOnly = true;
            return true;
        },
        "only parse the P4 input, without any further processing");
    registerOption(
        "--validate", nullptr,
        [this](const char *) {
            validateOnly = true;
            return true;
        },
        "Validate the P4 input, running just the front-end");
    registerOption(
        "--fromJSON", "file",
        [this](const char *arg) {
            loadIRFromJson = true;
            file = arg;
            return true;
        },
        "read previously dumped json instead of P4 source code");
    registerOption(
        "--turn-off-logn", nullptr,
        [](const char *) {
            ::P4::Log::Detail::enableLoggingGlobally = true;
            return true;
        },
        "Turn off LOGN() statements in the compiler.\n"
        "Use '@__debug' annotation to enable LOGN on "
        "the annotated P4 object within the source code.\n");
    registerOption(
        "--preferSwitch", nullptr,
        [this](const char *) {
            preferSwitch = true;
            return true;
        },
        "use passes that use general switch instead of action_run");
}

using OpenDescContext = P4CContextWithOptions<OpenDescOptions>;

class OpenDescPragmas : public P4::P4COptionPragmaParser {
    std::optional<IOptionPragmaParser::CommandLineOptions> tryToParse(
        const IR::Annotation *annotation) {
        std::cout<<"annotation: "<<annotation->name.toString()<<std::endl;
        if (annotation->name == "test_keep_opassign") {
            test_keepOpAssign = true;
            return std::nullopt;
        }
        return P4::P4COptionPragmaParser::tryToParse(annotation);
    }

public:
    OpenDescPragmas() : P4::P4COptionPragmaParser(true) {}

    bool test_keepOpAssign = false;
};


class StructLayoutTable : public Inspector {
public:
    std::set<const IR::Type_Struct*> used_structs;
    // structType → (fieldName → (offset,width))
    std::map<cstring, std::map<cstring, std::pair<unsigned,unsigned>>> layout;

    bool in_card2host = false;
    bool preorder(const IR::P4Control* control) override {
        if (control->getAnnotation(cstring("card2host"))) {
            std::cout << "Found @card2host control: " << control->name << std::endl;
            in_card2host = true;
            //visit(control->body);
            //return true;
            return true;
        }
        return false;
    }

    void postorder(const IR::P4Control* /*unused*/) override {
        in_card2host = false;       // leave the scope
    }

    bool preorder(const IR::Type_Struct *ts) override {

        if (!ts->name.name.startsWith("cmpt_")) return false;   // ignore others
        std::cout<<"Found completion"<<std::endl;
        std::cout<<"Type_Struct: "<<ts->getName()<<std::endl;

        unsigned cur = 0;
        std::map<cstring, std::pair<unsigned,unsigned>> info;
        for (auto f : ts->fields) {
            unsigned w = 0;
            if (auto *b = f->type->to<IR::Type_Bits>())      w = b->size;
            else if (f->type->is<IR::Type_Boolean>())        w = 1;
            else                                             continue; // nested – skip
            info.emplace(f->name, std::make_pair(cur, w));
            std::cout<<"\tfield: "<< f->getName()<< "["<<cur <<":"<<w+cur<<"]"<<std::endl;
            cur += w;
        }
        layout.emplace(ts->name, std::move(info));
        return false;                      // no deeper children we care about
    }

    bool preorder(const IR::Declaration_Variable *decl) override {
        if (in_card2host){
            std::cout << "\tDV: " << decl->getName()<<" "<<decl->type<<"\n";
            return false;
        }
        return true;
    }

    bool preorder(const IR::MethodCallStatement *mc) override {
        if (in_card2host){
            std::cout << "\tMethodCallStatement: " << mc->toString() << "\n";
            return false;
        }
        return true;   // ← keep traversing into the initializer expression
    }

    bool preorder(const IR::MethodCallExpression *mc) override {
        // if (!in_card2host) return true;
        ;
        if (in_card2host) {
            std::cout << "\tMethodCallExpression: " << mc->toString() << "\n";
            return false;
        }
        return true;   // ← keep traversing into the initializer expression
    }


    bool preorder(const IR::Parameter* param) override {
       // if (in_card2host) {
        if (in_card2host) {
            std::cout << "\tParameter: " << param->getName() <<" "<<param->type<< "\n";
            return false;
        }
        return true;
    }

    bool preorder(const IR::AssignmentStatement* expr) override {
        //  if (in_card2host && expr->type != nullptr) {
        if (in_card2host) {
            const auto lhs = expr->left;
            const auto rhs = expr->right;
            const auto* rhs_path = expr->right->to<IR::PathExpression>();

            std::cout << "\tAssignmentStatement: "  << expr->toString()<< "\n";
            if (rhs_path != nullptr)
                std::cout << "\tAssignmentStatement Path: "  << rhs_path->path<< "\n";

            std::cout << "\t\tLSH: "  << lhs->toString() << "\n";
            std::cout << "\t\tRHS: "  << rhs->toString()<<" is constant: "<<rhs->is<IR::Constant>()<< "\n";
            return false;
        }
        return true;
    }
    bool preorder(const IR::Expression* expr) override {
      //  if (in_card2host && expr->type != nullptr) {
        if (in_card2host)
        std::cout << "\tExpression: "  << expr->toString()<< "\n";
/*
            if (auto ts = expr->type->to<IR::Type_Struct>()) {
                std::cout<<"\tExpression: "<<ts->name<<std::endl;
            }*/
       // }
        return true;
    }
};


int main(int argc, char *const argv[]) {
    setup_gc_logging();
    P4::setup_signals();
    P4::AutoCompileContext autoP4TestContext(new OpenDescContext);

    auto &options = OpenDescContext::get().options();

    options.langVersion = CompilerOptions::FrontendVersion::P4_16;
    options.compilerVersion = cstring("OpenDesc 0.1");

    if (options.process(argc, argv) != nullptr) {
        if (options.loadIRFromJson == false) options.setInputFile();
    }
    if (::P4::errorCount() > 0) return 1;

    const IR::P4Program *program = nullptr;
    auto hook = options.getDebugHook();

    P4::DiagnosticCountInfo info;
    program = P4::parseP4File(options);
    info.emitInfo("PARSER");
    if (program != nullptr && ::P4::errorCount() == 0) {
        OpenDescPragmas testPragmas;
        program->apply(P4::ApplyOptionsPragmas(testPragmas));
        info.emitInfo("PASS P4COptionPragmaParser");
    }

    TopLevelTypeRegistrar tltr;
    Analyzer analyzer;
    ControlFlowGraph cfg;
    StructLayoutTable slt;
    RssHash rss_hash;
    SoftwareCostModel scm;
    program->apply(cfg);

    std::cout << "Collected Paths:\n";
    cfg.display_metadata_semantic();
    cfg.display_app_semantic();
    cfg.evaluate_paths();

    //scm.print_all_costs();
    std:cout<<"Hellow workdl";
    return 0;
}
