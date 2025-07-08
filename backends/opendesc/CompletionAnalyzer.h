//
// Created by slahmer on 7/2/25.
//

#ifndef COMPLETIONANALYZER_H
#define COMPLETIONANALYZER_H
#include "ir/visitor.h"

using namespace P4;
class CompletionAnalyzer : public Inspector{

    bool preorder(const IR::P4Control *ctl) override;
    bool preorder(const IR::AssignmentStatement *as) override;
};



#endif //COMPLETIONANALYZER_H
