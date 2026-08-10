#include "snapshot_legacy15_model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 4) { std::cerr << "weights scaler golden\n"; return 2; }
    sze_snapshot15::Model model; std::string error;
    if (!model.load(argv[1], argv[2], &error)) { std::cerr << error << '\n'; return 1; }
    std::ifstream in(argv[3], std::ios::binary); char magic[8]; std::uint32_t rows = 0;
    if (!in.read(magic, 8) || std::memcmp(magic, "S15GOLD1", 8) != 0 || !in.read(reinterpret_cast<char*>(&rows), 4) || rows == 0 || rows > 10000) return 1;
    std::vector<std::array<float,36> > raw(rows); std::vector<float> expected(rows); std::array<float,64> expected_hidden;
    if (!in.read(reinterpret_cast<char*>(raw.data()), raw.size()*sizeof(raw[0])) || !in.read(reinterpret_cast<char*>(expected.data()), expected.size()*4) || !in.read(reinterpret_cast<char*>(expected_hidden.data()), expected_hidden.size()*4)) return 1;
    sze_snapshot15::State state; float max_prediction = 0.0f;
    for (std::size_t i=0;i<rows;++i){float value=0;if(!model.predict(raw[i],&state,&value,&error)){std::cerr<<error<<'\n';return 1;}max_prediction=std::max(max_prediction,std::fabs(value-expected[i]));}
    float max_hidden=0.0f;for(std::size_t i=0;i<64;++i)max_hidden=std::max(max_hidden,std::fabs(state.hidden[i]-expected_hidden[i]));
    std::cout<<"rows="<<rows<<" max_prediction_abs="<<max_prediction<<" max_hidden_abs="<<max_hidden<<'\n';
    return (max_prediction <= 1.0e-5f && max_hidden <= 1.0e-5f) ? 0 : 1;
}
