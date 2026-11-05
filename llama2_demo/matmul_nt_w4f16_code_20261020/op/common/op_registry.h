#ifndef OP_REGISTRY_H
#define OP_REGISTRY_H

/**
 * @file op_registry.h
 * @brief 算子注册器：管理所有算子的注册和动态获取
 * 
 * 设计理念：
 * - 单例模式，全局唯一实例
 * - 支持算子的自动注册和动态获取
 * - 提供算子列表查询功能
 * - 简化TVGen与算子的交互
 */

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "op_base.h"

namespace aiacc {
namespace op {

/**
 * @brief 算子注册器
 * 
 * 单例类，管理所有算子的注册和获取
 */
class OpRegistry {
public:
    /**
     * @brief 获取注册器实例（单例模式）
     * @return 注册器引用
     */
    static OpRegistry& get_instance() {
        static OpRegistry instance;
        return instance;
    }
    
    /**
     * @brief 注册算子
     * @param op 算子对象的unique_ptr
     */
    void register_op(std::unique_ptr<OpBase> op) {
        if (op) {
            ops_[op->get_name()] = std::move(op);
        }
    }
    
    /**
     * @brief 根据名称获取算子
     * @param name 算子名称
     * @return 算子指针，找不到返回nullptr
     */
    OpBase* get_op(const std::string& name) {
        auto it = ops_.find(name);
        if (it != ops_.end()) {
            return it->second.get();
        }
        return nullptr;
    }
    
    /**
     * @brief 获取所有算子名称
     * @return 算子名称列表
     */
    std::vector<std::string> get_all_op_names() {
        std::vector<std::string> names;
        for (const auto& pair : ops_) {
            names.push_back(pair.first);
        }
        return names;
    }
    
    /**
     * @brief 禁止拷贝构造和赋值操作
     */
    OpRegistry(const OpRegistry&) = delete;
    OpRegistry& operator=(const OpRegistry&) = delete;
    
private:
    /**
     * @brief 私有构造函数（单例模式）
     */
    OpRegistry() = default;
    
    /**
     * @brief 算子映射表：名称 -> 算子对象
     */
    std::map<std::string, std::unique_ptr<OpBase>> ops_;
};

// 注册宏
#define REGISTER_OP(op_class) \
    class op_class##_Registrar { \
    public: \
        op_class##_Registrar() { \
            OpRegistry::get_instance().register_op(std::make_unique<op_class>()); \
        } \
    }; \
    op_class##_Registrar g_##op_class##_registrar;

} // namespace op
} // namespace aiacc

#endif // OP_REGISTRY_H