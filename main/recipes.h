#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include "esp_err.h"

struct Recipe {
    std::string name;
    struct Ingredient { uint8_t pump_id; uint16_t amount_ml; };
    std::array<Ingredient, 4> ingredients{};
    uint8_t num_ingredients = 0;
};

class RecipeStore {
public:
    static RecipeStore &getInstance();
    esp_err_t init();
    std::vector<Recipe> all() const;
    bool get(size_t index, Recipe &recipe) const;
    esp_err_t add(const Recipe &recipe);
    esp_err_t replaceAll(const std::vector<Recipe> &replacement);
private:
    RecipeStore() = default;
    esp_err_t save();
    mutable std::mutex mutex;
    std::vector<Recipe> recipes;
};
