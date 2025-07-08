

#include "frontends/common/options.h"

using namespace P4;

class OpenDescOptions : public CompilerOptions {
public:
    bool parseOnly = false;
    bool validateOnly = false;
    bool loadIRFromJson = false;
    bool preferSwitch = false;
    OpenDescOptions();
};
