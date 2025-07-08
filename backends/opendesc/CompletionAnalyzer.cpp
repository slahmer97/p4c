//
// Created by slahmer on 7/2/25.
//

#include "CompletionAnalyzer.h"

#include "ir/ir-generated.h"

bool CompletionAnalyzer::preorder(const IR::P4Control *ctl) {
    //std::cout<<"Completion: "<<ctl->getName()<<std::endl;

}

bool CompletionAnalyzer::preorder(const IR::AssignmentStatement *as) {


}