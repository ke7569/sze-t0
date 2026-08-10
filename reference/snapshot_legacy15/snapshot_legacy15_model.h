#ifndef SZE_SNAPSHOT_LEGACY15_MODEL_H
#define SZE_SNAPSHOT_LEGACY15_MODEL_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace sze_snapshot15 {

struct State {
    std::array<float, 64> hidden;
    State();
    void reset();
};

class Model {
public:
    struct Tensor { std::vector<std::uint32_t> shape; std::vector<float> values; };
    Model();
    bool load(const std::string& weights_path, const std::string& scaler_path,
              std::string* error);
    bool loaded() const { return loaded_; }
    bool predict(const std::array<float, 36>& raw, State* state, float* output,
                 std::string* error = 0) const;

private:
    Tensor proj_weight_, proj_bias_;
    Tensor f0_weight_, f0_bias_, f2_weight_, f2_bias_, f4_weight_, f4_bias_;
    Tensor gru_ih_, gru_hh_, gru_bih_, gru_bhh_;
    Tensor res_weight_, res_bias_, ln_weight_, ln_bias_;
    Tensor head0_weight_, head0_bias_, head2_weight_, head2_bias_;
    std::array<float, 36> mean_{};
    std::array<float, 36> scale_{};
    bool loaded_ = false;

    static bool read_tensor(std::ifstream* in, std::string* name, Tensor* tensor,
                            std::string* error);
    static float softsign(float value);
    static float sigmoid(float value);
    static bool finite_array(const std::array<float, 36>& values);
    static bool finite_vector(const std::vector<float>& values);
};

}  // namespace sze_snapshot15

#endif
