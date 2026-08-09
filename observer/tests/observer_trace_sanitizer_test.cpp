#include "ObserverTraceSanitizer.h"

#include <cassert>
#include <iostream>
#include <string>

int main() {
    beammp::observer::TraceSanitizer sanitizer;
    const std::string raw = R"({"tim":1.25,"pos":[1,2,3],"rot":[0,0,0,1],"vel":[4,5,6],"rvel":[7,8,9],"ping":99,"ip":"never-copy","chat":"never-copy"})";

    const auto line = sanitizer.Sanitize(42, 9, 1'000'000, raw);
    assert(line.has_value());
    assert(line->find("\"player\":0") != std::string::npos);
    assert(line->find("\"vehicle\":0") != std::string::npos);
    assert(line->find("\"pos\":[1.0,2.0,3.0]") != std::string::npos);
    assert(line->find("ip") == std::string::npos);
    assert(line->find("chat") == std::string::npos);
    assert(line->find("ping") == std::string::npos);
    assert(!sanitizer.Sanitize(42, 9, 1'000'001, R"({"pos":[1,2]})").has_value());
    std::cout << "observer trace sanitizer tests passed\n";
}
