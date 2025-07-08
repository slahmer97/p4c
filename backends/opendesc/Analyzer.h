//
// Created by slahmer on 7/2/25.
//

#ifndef ANALYZER_H
#define ANALYZER_H
#include "ir/visitor.h"
using namespace P4;
class Analyzer: public Inspector{
public:
    std::map<cstring, const IR::Type_Header*> semanticFields;
    const IR::Type_Struct* metadataStruct = nullptr;
    const IR::Type_Struct* completionStruct = nullptr;
    cstring targetField;
    cstring metadataField;

    bool preorder(const IR::Type_Header* type) override;
    bool preorder(const IR::Type_Struct* type) override;
    bool preorder(const IR::P4Control* ctl) override;
    bool preorder(const IR::AssignmentStatement* stmt) override;
    void analyzeControlFlow(const IR::P4Program* program);
    void generateAccessors(std::ostream& out);
};



#endif //ANALYZER_H
