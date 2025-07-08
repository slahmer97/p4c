//
// Created by slahmer on 7/7/25.
//

#ifndef SOFTWARECOSTMODEL_H
#define SOFTWARECOSTMODEL_H
#include "ir/ir-generated.h"

using namespace P4;

class SoftwareCostModel {
public:
    SoftwareCostModel() {
        semantic_costs["rss"] = 100;            // RSS hash (e.g., Toeplitz)
        semantic_costs["vlan"] = 5;             // VLAN tag extraction
        semantic_costs["checksum:ip"] = 10;     // IP header checksum
        semantic_costs["checksum:udp"] = 30;    // UDP checksum (with pseudo-header)
        semantic_costs["checksum:tcp"] = 50;    // TCP checksum (potentially expensive)
    }

    int get_cost(const std::string& semantic) const {
        auto it = semantic_costs.find(semantic);
        if (it != semantic_costs.end()) {
            return it->second;
        }
        return default_cost;
    }

    void print_all_costs() const {
        std::cout << "=== Software Semantic Costs ===\n";
        for (const auto& [semantic, cost] : semantic_costs) {
            std::cout << "Semantic: " << semantic << " ---> Cost: " << cost << "\n";
        }
        std::cout << "===============================\n";
    }

private:
    std::map<std::string, int> semantic_costs;
    const int default_cost = 15;
};



#endif //SOFTWARECOSTMODEL_H
