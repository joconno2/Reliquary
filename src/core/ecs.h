#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <memory>
#include <typeindex>
#include <cassert>

using Entity = uint32_t;
constexpr Entity NULL_ENTITY = 0;

// Type-erased component storage base
class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual void remove(Entity e) = 0;
    virtual bool has(Entity e) const = 0;
};

// Typed component storage — dense array with entity lookup
template<typename T>
class ComponentPool : public IComponentPool {
public:
    T& add(Entity e, T component = {}) {
        assert(!has(e));
        entity_to_index_[e] = data_.size();
        index_to_entity_.push_back(e);
        data_.push_back(std::move(component));
        return data_.back();
    }

    void remove(Entity e) override {
        if (!has(e)) return;
        size_t index = entity_to_index_[e];
        size_t last = data_.size() - 1;

        if (index != last) {
            data_[index] = std::move(data_[last]);
            Entity moved = index_to_entity_[last];
            entity_to_index_[moved] = index;
            index_to_entity_[index] = moved;
        }

        data_.pop_back();
        index_to_entity_.pop_back();
        entity_to_index_.erase(e);
    }

    bool has(Entity e) const override {
        return entity_to_index_.count(e) > 0;
    }

    T& get(Entity e) {
        assert(has(e));
        return data_[entity_to_index_[e]];
    }

    const T& get(Entity e) const {
        assert(has(e));
        return data_[entity_to_index_.at(e)];
    }

    // Iteration
    size_t size() const { return data_.size(); }
    T& at_index(size_t i) { return data_[i]; }
    Entity entity_at(size_t i) const { return index_to_entity_[i]; }

    // Range-based for support: iterate (entity, component) pairs
    struct Iterator {
        ComponentPool& pool;
        size_t index;
        bool operator!=(const Iterator& other) const { return index != other.index; }
        Iterator& operator++() { ++index; return *this; }
        std::pair<Entity, T&> operator*() {
            return {pool.index_to_entity_[index], pool.data_[index]};
        }
    };
    Iterator begin() { return {*this, 0}; }
    Iterator end() { return {*this, data_.size()}; }

private:
    std::vector<T> data_;
    std::vector<Entity> index_to_entity_;
    std::unordered_map<Entity, size_t> entity_to_index_;
};

class World {
public:
    World() : next_entity_(1) {}

    Entity create() { return next_entity_++; }

    void destroy(Entity e) {
        for (auto& [type, pool] : pools_) {
            pool->remove(e);
        }
        destroyed_.push_back(e);
    }

    bool is_alive(Entity e) const {
        // Quick check — if it has any component, it's alive
        // Otherwise check if it was ever created but not destroyed
        for (auto& [type, pool] : pools_) {
            if (pool->has(e)) return true;
        }
        return false;
    }

    template<typename T>
    T& add(Entity e, T component = {}) {
        return get_pool<T>().add(e, std::move(component));
    }

    template<typename T>
    void remove(Entity e) {
        get_pool<T>().remove(e);
    }

    template<typename T>
    bool has(Entity e) const {
        auto it = pools_.find(std::type_index(typeid(T)));
        if (it == pools_.end()) return false;
        return static_cast<const ComponentPool<T>*>(it->second.get())->has(e);
    }

    template<typename T>
    T& get(Entity e) {
        return get_pool<T>().get(e);
    }

    template<typename T>
    const T& get(Entity e) const {
        auto it = pools_.find(std::type_index(typeid(T)));
        assert(it != pools_.end());
        return static_cast<const ComponentPool<T>*>(it->second.get())->get(e);
    }

    template<typename T>
    ComponentPool<T>& pool() {
        return get_pool<T>();
    }

    Entity entity_count() const { return next_entity_ - 1; }

private:
    Entity next_entity_;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> pools_;
    std::vector<Entity> destroyed_;

    template<typename T>
    ComponentPool<T>& get_pool() {
        auto key = std::type_index(typeid(T));
        auto it = pools_.find(key);
        if (it == pools_.end()) {
            auto pool = std::make_unique<ComponentPool<T>>();
            auto& ref = *pool;
            pools_[key] = std::move(pool);
            return ref;
        }
        return *static_cast<ComponentPool<T>*>(it->second.get());
    }
};
