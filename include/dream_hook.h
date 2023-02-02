#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <any>
#include <algorithm>
#include <string>
#include <map>
#include <vector>

namespace dream {

template<typename T>
class Hookable {

    using HookCallback = std::function<void(T&, const std::any& data)>;
    using GlobalHookCallback = std::function<bool(T&, const std::string&, const std::any& data)>;

    static inline std::atomic<uint64_t> _hook_id_counter = 0;

    std::shared_mutex trigger_mtx, unregister_mtx;

    std::map<uint64_t, GlobalHookCallback> cb_global_hooks;
    std::map<std::string, std::map<uint64_t, HookCallback>> cb_hooks;

public:
    Hookable() = default;
    virtual ~Hookable() = default;

    uint64_t register_hook(const std::string& hook_name, HookCallback hook) {
        std::unique_lock<std::shared_mutex> lock(trigger_mtx);
        uint64_t id = _hook_id_counter++;

        if(!cb_hooks.count(hook_name)){
            cb_hooks.insert_or_assign(hook_name, std::map<uint64_t, HookCallback> {}); // insert fresh empty map
        }

        auto& hooks = cb_hooks.at(hook_name);

        hooks.insert_or_assign(id, hook);

        return id;
    }

    uint64_t register_global_hook(GlobalHookCallback hook) {
        std::unique_lock<std::shared_mutex> lock(trigger_mtx);
        uint64_t id = _hook_id_counter++;

        cb_global_hooks.insert_or_assign(id, hook);

        return id;
    }

    void unregister_hook(uint64_t id) {
        std::unique_lock<std::shared_mutex> lock_trig(trigger_mtx, std::defer_lock);
        std::unique_lock<std::shared_mutex> lock_unreg(unregister_mtx, std::defer_lock);

        std::scoped_lock lock(lock_trig, lock_unreg);

        if(cb_global_hooks.count(id)){ // check the global hooks for a trigger that contains the hook id
            cb_global_hooks.erase(id);
            return;
        }

        for(auto& [_, hooks] : cb_hooks){
            if(hooks.count(id)){ // find the hook trigger that contains the hook id
                hooks.erase(id);
                return;
            }
        }
    }

protected:
    void trigger_hook(const std::string& hook_name, const std::any& data = {}) {
        std::shared_lock<std::shared_mutex> lock_trig(trigger_mtx, std::defer_lock);
        std::shared_lock<std::shared_mutex> lock_unreg(unregister_mtx, std::defer_lock);

        std::scoped_lock lock(lock_trig, lock_unreg);

        for(auto& [id, cb] : cb_global_hooks){
            lock_trig.unlock();
            if(!cb(*static_cast<T*>(this), hook_name, data))
                return; // check for global hook override - false return will abort remaining hook triggers
            lock_trig.lock();
        }

        if(!cb_hooks.count(hook_name)){
            return;
        }

        auto& hook_list = cb_hooks.at(hook_name);
        for(auto& [id, cb] : hook_list){
            lock_trig.unlock();
            cb(*static_cast<T*>(this), data);
            lock_trig.lock();
        }
    }

};


}