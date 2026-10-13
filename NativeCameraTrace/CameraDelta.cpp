#include "CameraDelta.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

namespace FH3Trace {
CameraDelta::CameraDelta(std::size_t bytes) : bytes_((bytes / 4) * 4),
    fields_(bytes_ / 4), distinct_(bytes_ / 4) {
    for(std::size_t i=0; i<fields_.size(); ++i) fields_[i].offset=i*4;
}
bool CameraDelta::Add(const std::vector<std::uint8_t>& snapshot) {
    if(snapshot.size()!=bytes_ || bytes_==0) return false;
    for(std::size_t i=0; i<fields_.size(); ++i) {
        std::uint32_t value=0;
        std::memcpy(&value,snapshot.data()+i*4,sizeof(value));
        auto& field=fields_[i];
        auto& distinct=distinct_[i];
        if(samples_==0) field.initial=value;
        else if(field.last!=value) ++field.transitions;
        field.last=value;
        if(std::find(distinct.begin(),distinct.end(),value)==distinct.end() &&
           distinct.size()<9) distinct.push_back(value);
        field.distinctValues=distinct.size();
    }
    ++samples_;
    return true;
}
std::vector<FieldChange> CameraDelta::SparseChanges(std::size_t maxTransitions) const {
    std::vector<FieldChange> result;
    for(const auto& field:fields_)
        if(field.transitions>0 && field.transitions<=maxTransitions)
            result.push_back(field);
    std::sort(result.begin(),result.end(),[](const FieldChange& a,const FieldChange& b) {
        if(a.transitions!=b.transitions) return a.transitions<b.transitions;
        if(a.distinctValues!=b.distinctValues) return a.distinctValues<b.distinctValues;
        return a.offset<b.offset;
    });
    return result;
}
} // namespace FH3Trace
