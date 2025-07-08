//
// Created by slahmer on 7/4/25.
//

#ifndef TOPLEVELTYPEREGISTRAR_H
#define TOPLEVELTYPEREGISTRAR_H
#include "frontends/common/resolveReferences/referenceMap.h"
#include "ir/ir-generated.h"
#include "ir/visitor.h"


using namespace P4;
struct RegisteredType {
    IR::ID paramName;
    const IR::Type *structType = nullptr;
};

class TopLevelTypeRegistrar : public Inspector {
public:
    RegisteredType c2h_context;
    RegisteredType h2c_desc;
    RegisteredType metadata;
    bool in_card2host = false;
    unsigned int m_param_rank = 0;

    bool preorder(const IR::P4Control* control) override {
        if (control->getAnnotation(cstring("card2host"))) {
            std::cout << "Found @card2host control: " << control->name << std::endl;
            in_card2host = true;
            return true;
        }
        return false;
    }

    void postorder(const IR::P4Control* /*unused*/) override {
        in_card2host = false;       // leave the scope
    }

    bool preorder(const IR::Parameter* param) override {
        if (in_card2host) {

            //std::cout << "\tParameter " << m_param_rank << ": " << param->getName()
             //                 << " : " << param->type << "\n";

            switch (m_param_rank) {
                case 1:
                    c2h_context.paramName = param->getName();
                    c2h_context.structType = param->type;
                    std::cout<<"\tParameter param-name: "<<c2h_context.paramName<<" structType: "<<c2h_context.structType<<std::endl;
                    break;
                case 3:
                    h2c_desc.paramName = param->getName();
                    h2c_desc.structType = param->type;
                    std::cout<<"\tParameter param-name: "<<h2c_desc.paramName<<" structType: "<<h2c_desc.structType<<std::endl;
                    break;
                case 4:
                    metadata.paramName = param->getName();
                    metadata.structType = param->type;
                    std::cout<<"\tParameter param-name: "<<metadata.paramName<<" structType: "<<metadata.structType<<std::endl;
                    break;
                default:
                    //std::cout << "\tParameter: " << param->getName() <<" "<<param->type<< "\n";
                    break;
            }
            m_param_rank += 1;
        }
        return false;
    }
};


#endif //TOPLEVELTYPEREGISTRAR_H
