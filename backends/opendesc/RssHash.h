//
// Created by slahmer on 7/4/25.
//

#ifndef RSSHASH_H
#define RSSHASH_H
#include "ir/ir-generated.h"
#include "ir/visitor.h"

using namespace P4;
class RssHash : public P4::Inspector {

    bool main_control = false;
    bool preorder(const IR::P4Control* control) override {
        std::cout << control->name << std::endl;
        if (control->getName() == "MainControl") {
            std::cout << "Found main_control" << control->name << std::endl;
            main_control = true;
            return true;
        }
        return false;
    }

    bool preorder(const IR::AssignmentStatement* expr) override {
        //  if (in_card2host && expr->type != nullptr) {
        if (main_control) {
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

};



#endif //RSSHASH_H
