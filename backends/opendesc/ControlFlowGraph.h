//
// Created by slahmer on 7/6/25.
//

#ifndef CONTROLFLOWGRAPH_H
#define CONTROLFLOWGRAPH_H
#include <fstream>

#include "SoftwareCostModel.h"
#include "ir/ir-generated.h"
#include "ir/visitor.h"
using namespace P4;

struct SemanticInfo {
    cstring semantic_value;
    const IR::StructField* field;
    const IR::AnnotationToken* annotation;
};


class ControlFlowGraph  : public Inspector {
public:
    std::vector<std::vector<const IR::MethodCallStatement*>> all_paths;
    std::map<cstring, SemanticInfo> semantic_field_map;
    std::map<cstring, SemanticInfo> app_semantic_fields;
    SoftwareCostModel scm;
    std::map<cstring, int> field_to_size;

    bool in_card2host = false;
    bool preorder(const IR::P4Control* control) override {
        if (control->getAnnotation(cstring("card2host"))) {
            std::cout << "Found @card2host control: " << control->name << std::endl;
            in_card2host = true;

            if (control->body != nullptr) {
                for (const auto* stmt : control->body->components) {
                    if (auto mc = stmt->to<IR::MethodCallStatement>()) {
                        std::cout << "\tMethodCallStatement (leaf): " << mc->toString() << "\n";
                        append_to_all_paths(mc);
                    } else if (auto ifs = stmt->to<IR::IfStatement>()) {
                        //visit(ifs);
                        std::cout << "\tIfStatement condition: " << ifs->condition << "\n";
                        std::cout << "\tIfStatement condition: " << ifs->ifTrue << "\n";
                        std::cout << "\tIfStatement condition: " << ifs->ifFalse << "\n";
                        //no support for nested if/switch yet
                        auto path1 = get_all_calls(ifs->ifTrue);
                        auto path2 = get_all_calls(ifs->ifFalse);
                        auto vec = std::vector{path1, path2};
                        append_paths(vec);
                    }
                }
            }

            return false;  // Stop automatic traversal
        }
        return false;
    }

    std::vector<const IR::MethodCallStatement*> get_all_calls(const IR::Statement* stmt) {
        std::vector<const IR::MethodCallStatement*> calls;

        if (auto block = stmt->to<IR::BlockStatement>()) {
            for (const auto* s : block->components) {
                if (auto mc = s->to<IR::MethodCallStatement>()) {
                    std::cout << "\t\tMethodCallStatement (leaf)1: " << mc->toString() << "\n";
                    calls.push_back(mc);
                } else if (auto nested_if = s->to<IR::IfStatement>()) {
                    std::cout<<"Not implemented"<<std::endl;
                }
            }
        } else if (auto mc = stmt->to<IR::MethodCallStatement>()) {
            std::cout << "\t\tMethodCallStatement (leaf)2: " << mc->toString() << "\n";

            calls.push_back(mc);
        }
        return calls;
    }

    std::string get_cond_path(const IR::IfStatement* stmt) {
        return stmt->toString().c_str();
    }

    void append_paths(const std::vector<std::vector<const IR::MethodCallStatement*>>& paths) {
        std::vector<std::vector<const IR::MethodCallStatement*>> new_paths;

        for (const auto& base_path : all_paths) {
            for (const auto& extension : paths) {
                std::vector<const IR::MethodCallStatement*> combined = base_path;
                combined.insert(combined.end(), extension.begin(), extension.end());
                new_paths.push_back(std::move(combined));
            }
        }

        all_paths = std::move(new_paths);
    }

    void append_to_all_paths(const IR::MethodCallStatement* stmt) {
        if (all_paths.empty()) {
            all_paths.emplace_back(std::vector{stmt});
            return;
        }
        for (auto& path : all_paths) {
            path.push_back(stmt);
        }
    }

    bool preorder(const IR::Type_Struct* ts) override {
        std::cout<<"Type_Struct: "<<ts->toString() << " " << ts->getDeclarations()<<std::endl;

        // TODO change hardcoding struct names to flexible ones
        if (ts->name == "meta_t"){
            for (const auto* field : ts->fields) {
                field_to_size[field->name] = field->type->width_bits();
                if (field->getAnnotations().empty())
                    continue;
                auto ann = field->getAnnotations()[0];
                std::cout << "\tField: " << field->name << "\t"<<ann->getUnparsed()[0] << field->type << std::endl;
                semantic_field_map[field->name] = SemanticInfo{
                    .semantic_value = ann->name,
                    .field = field,
                    .annotation = ann->getUnparsed()[0]
                };
            }
        }else if (ts->name == "intent_t") {
            for (const auto* field : ts->fields) {
                if (field->getAnnotations().empty())
                    continue;
                auto ann = field->getAnnotations()[0];
                std::cout << "\tField: " << field->name << "\t"<<ann->getUnparsed()[0] << std::endl;
                app_semantic_fields[ann->getUnparsed()[0]->text] = SemanticInfo{
                    .semantic_value = ann->name,
                    .field = field,
                    .annotation = ann->getUnparsed()[0]
                };
            }
        }
        return false;
    }


    void postorder(const IR::P4Control* /*unused*/) override {
        std::cout<<"Postorder"<<std::endl;
    }

    void display_paths() const {
        std::cout << "Collected Paths:\n";
        for (size_t i = 0; i < all_paths.size(); ++i) {
            std::cout << "Path " << i + 1 << ": ";
            for (const auto* stmt : all_paths[i]) {
                std::cout << stmt->toString() << " ";
            }
            std::cout << "\n";
        }
    }

    void evaluate_paths() {
        int min = 0;
        int val = 10000;
        for (unsigned int path_index = 0; path_index < all_paths.size(); ++path_index) {
            std::cout << "Path ---------------------------" << path_index + 1 << ": "<<std::endl;
            auto tmp = evaluate_path(path_index);
            if (tmp < val) {
                val = tmp;
                min = path_index;
            }
        }
        std::cout<<"Winning path is :"<<min<<std::endl;
        std::ofstream file("/home/slahmer/CLionProjects/p4c/accessors.h");
        generate_code(min, file);
    }

    int evaluate_path(unsigned int path_index) {
        auto path = all_paths[path_index];
        std::map<cstring, bool> path_semantics;
        for (const auto* stmt : path) {
            for (const auto* arg : *stmt->methodCall->arguments) {
                std::cout << arg->toString() << "\t";
                const auto* member = arg->expression->to<IR::Member>();
                if (!member) continue;
                const auto* base = member->expr->to<IR::PathExpression>();
                std::cout << base->toString()<<" "<<base->type << " " << member->member.name << "\t";
                cstring field_name = member->member.name;
                if (semantic_field_map.find(field_name) != semantic_field_map.end()) {
                    auto semantic = semantic_field_map[field_name].annotation->text;
                    std::cout<<"\t\tKey  "<<field_name<< "  "<< semantic<<std::endl;
                    if (app_semantic_fields.find(semantic) != app_semantic_fields.end()) {
                        std::cout<<"Found a requested semantic"<<std::endl;
                        path_semantics[semantic] = true;
                    }
                }
                else {
                    std::cout<<"\t\tKey  "<<field_name<< "  no sem"<<std::endl;
                }
            }

            std::cout<< "\n";
        }
        int total_cost = 0;
        for (const auto& required : app_semantic_fields) {
            if (!path_semantics.count(required.first)) {
                int cost = scm.get_cost(required.first.c_str());
                total_cost += cost;
                std::cout << " Missing semantic: " << required.first
                          << " -> Software fallback cost: " << cost << "\n";
            }
        }
        return total_cost;
    }

    void generate_code(unsigned int path_idx, std::ostream& out) {
        auto path = all_paths[path_idx];
        std::map<cstring, bool> path_semantics;
        unsigned int curr = 0;
        out << "\n// === Generated Accessor Functions ===\n";
        for (const auto* stmt : path) {
            for (const auto* arg : *stmt->methodCall->arguments) {
                const auto* member = arg->expression->to<IR::Member>();
                if (!member) continue;
                const auto* base = member->expr->to<IR::PathExpression>();
                cstring field_name = member->member.name;
                std::cout<<"name "<<field_name<< "  type:"<<member->type<<std::endl;
                int width = field_to_size[field_name];
                int width_bytes = (width + 7) / 8;              // round up
                int byte_offset = curr / 8;                    // truncate to byte offset

                if (semantic_field_map.find(field_name) != semantic_field_map.end()) {
                    auto semantic = semantic_field_map[field_name].annotation->text;
                    if (app_semantic_fields.find(semantic) != app_semantic_fields.end()) {
                        path_semantics[semantic] = true;
                        // generate function!
                        //out << "uint32_t get_" << field_name << "_hw(void* completion) {\n";
                        //out << "    uint8_t* ptr = (uint8_t*)completion + " << byte_offset << ";\n";

                        if (width <= 8) {
                            out << "uint8_t get_" << field_name << "_hw(void* completion) {\n";
                            out << "    uint8_t* ptr = (uint8_t*)completion + " << byte_offset << ";\n";
                            out << "    return *ptr & 0xFF;\n";
                        } else if (width <= 16) {
                            out << "uint16_t get_" << field_name << "_hw(void* completion) {\n";
                            out << "    uint8_t* ptr = (uint8_t*)completion + " << byte_offset << ";\n";
                            out << "    return *((uint16_t*)ptr) & 0xFFFF;\n";
                        } else if (width <= 32) {
                            out << "uint32_t get_" << field_name << "_hw(void* completion) {\n";
                            out << "    uint8_t* ptr = (uint8_t*)completion + " << byte_offset << ";\n";
                            out << "    return *((uint32_t*)ptr);\n";
                        } else {
                            out << "    // Field too wide: " << width << " bits\n";
                            out << "    return 0; // unsupported width\n";
                        }

                        out << "}\n\n";
                    }
                }
                curr += width;
            }
        }

    }
    void display_metadata_semantic() const {
        std::cout << "\n=== Semantic Field Map ===\n";
        for (const auto& [field_name, info] : semantic_field_map) {
            std::cout << "Field: " << field_name
                      << " | Semantic: \"" << info.semantic_value << "\""
                      << " | StructField: " << info.field->toString()
                      << " | Raw Annotation: " << info.annotation
                      << "\n";
        }
        std::cout << "=== End of Map ===\n";
    }

    void display_app_semantic() const {
        std::cout << "\n=== Semantic Field Map ===\n";
        for (const auto& [field_name, info] : app_semantic_fields) {
            std::cout << "Field: " << field_name
                      << " | Semantic: \"" << info.semantic_value << "\""
                      << " | StructField: " << info.field->toString()
                      << " | Raw Annotation: " << info.annotation
                      << "\n";
        }
        std::cout << "=== End of Map ===\n";
    }




};






#endif //CONTROLFLOWGRAPH_H
