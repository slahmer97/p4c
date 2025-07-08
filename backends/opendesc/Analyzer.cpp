//
// Created by slahmer on 7/2/25.
//

#include "Analyzer.h"

#include "ir/ir-generated.h"

bool Analyzer::preorder(const IR::Type_Header* type) {
    std::cout<<type->getName()<<std::endl;
    for (auto field : type->fields) {
        field->getAnnotations();
        std::cout<<"\tNode: "<<field->getName()<<" with annotation: "<<field->getAnnotations()<<std::endl;
        /*
        auto anno = field->getAnnotation("semantic");
        if (anno) {
            semanticFields[anno->getName()] = type;
            targetField = field->name;
        }
        */
    }
    return false;
}

bool Analyzer::preorder(const IR::Type_Struct* type) {

    if (type->name == "meta_t") metadataStruct = type;
    if (type->name.name.startsWith("cmpt_")) completionStruct = type;
    return false;
}


bool Analyzer::preorder(const IR::AssignmentStatement* stmt) {
    // Track value assignments
    //valueMap[stmt->left] = stmt->right;
    std::cout<<"Statement assignment :"<<stmt<<std::endl;
    return false;
}
bool Analyzer::preorder(const IR::P4Control* ctl) {
    std::cout<<"Node CTRL: "<<ctl->getName()<< "annotations: "<<ctl->getAnnotations() <<std::endl;
    if (ctl->getAnnotation(cstring("card2host"))) {
        std::cout<<"Foudddddddddddddddn";
    }
    return true;
}

void Analyzer::analyzeControlFlow(const IR::P4Program* program) {


}

