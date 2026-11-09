#include "recipes.h"
#include "config.h"
#include "nvs.h"
#include <cstring>

namespace {
struct StoredRecipe { char name[25]; uint16_t amount_ml[4]; };
const Recipe defaults[] = {
    {"Mojito", {{{0, 50}, {1, 20}, {2, 80}, {3, 10}}}, 4},
    {"Cuba Libre", {{{0, 50}, {1, 10}, {2, 100}, {0, 0}}}, 3},
};
}

RecipeStore &RecipeStore::getInstance() { static RecipeStore store; return store; }
esp_err_t RecipeStore::init() {
    std::lock_guard<std::mutex> lock(mutex); nvs_handle_t nvs;
    if (nvs_open("recipes", NVS_READONLY, &nvs) == ESP_OK) {
        size_t size = 0;
        if (nvs_get_blob(nvs, "items", nullptr, &size) == ESP_OK && size && size % sizeof(StoredRecipe) == 0) {
            std::vector<StoredRecipe> stored(size / sizeof(StoredRecipe));
            if (stored.size() <= MAX_COCKTAILS && nvs_get_blob(nvs, "items", stored.data(), &size) == ESP_OK) {
                for (const auto &item : stored) {
                    Recipe recipe; recipe.name.assign(item.name, strnlen(item.name, sizeof(item.name)));
                    for (uint8_t pump = 0; pump < 4; ++pump) if (item.amount_ml[pump]) recipe.ingredients[recipe.num_ingredients++] = {pump, item.amount_ml[pump]};
                    if (!recipe.name.empty() && recipe.num_ingredients) recipes.push_back(recipe);
                }
            }
        }
        nvs_close(nvs);
    }
    if (recipes.empty()) recipes.assign(std::begin(defaults), std::end(defaults));
    return ESP_OK;
}
std::vector<Recipe> RecipeStore::all() const { std::lock_guard<std::mutex> lock(mutex); return recipes; }
bool RecipeStore::get(size_t index, Recipe &recipe) const { std::lock_guard<std::mutex> lock(mutex); if (index >= recipes.size()) return false; recipe = recipes[index]; return true; }
esp_err_t RecipeStore::add(const Recipe &recipe) {
    if (recipe.name.empty() || recipe.name.size() > 24 || !recipe.num_ingredients) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(mutex); if (recipes.size() >= MAX_COCKTAILS) return ESP_ERR_NO_MEM;
    recipes.push_back(recipe); esp_err_t result = save(); if (result != ESP_OK) recipes.pop_back(); return result;
}
esp_err_t RecipeStore::replaceAll(const std::vector<Recipe> &replacement) {
    if (replacement.empty() || replacement.size() > MAX_COCKTAILS) return ESP_ERR_INVALID_ARG;
    for (const auto &recipe : replacement) {
        if (recipe.name.empty() || recipe.name.size() > 24 || !recipe.num_ingredients || recipe.num_ingredients > 4) {
            return ESP_ERR_INVALID_ARG;
        }
        for (uint8_t i = 0; i < recipe.num_ingredients; ++i) {
            if (recipe.ingredients[i].pump_id >= 4 || recipe.ingredients[i].amount_ml == 0) return ESP_ERR_INVALID_ARG;
        }
    }
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<Recipe> previous = recipes;
    recipes = replacement;
    esp_err_t result = save();
    if (result != ESP_OK) recipes.swap(previous);
    return result;
}
esp_err_t RecipeStore::save() {
    std::vector<StoredRecipe> stored(recipes.size());
    for (size_t i = 0; i < recipes.size(); ++i) {
        std::strncpy(stored[i].name, recipes[i].name.c_str(), sizeof(stored[i].name) - 1);
        for (uint8_t j = 0; j < recipes[i].num_ingredients; ++j) { auto in = recipes[i].ingredients[j]; if (in.pump_id < 4) stored[i].amount_ml[in.pump_id] = in.amount_ml; }
    }
    nvs_handle_t nvs; esp_err_t result = nvs_open("recipes", NVS_READWRITE, &nvs); if (result != ESP_OK) return result;
    result = nvs_set_blob(nvs, "items", stored.data(), stored.size() * sizeof(StoredRecipe)); if (result == ESP_OK) result = nvs_commit(nvs); nvs_close(nvs); return result;
}
