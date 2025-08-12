#pragma once 

#include <atomic>
#include <vector>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <unordered_map>

namespace stel {

/// Event<Ts...> is a thread-safe publisher/subscriber for callbacks of signature:
///		void(const Ts&...);
///
///	- publish(...) is a lock-free with respect to other publisher/subscribers:
///	  it holds a snapshot of the current subscriber list and calls them.
/// - subscribe/unsubscribe are copy-on-write (small mutex), safe to call 
///   concurrently with publish and with each other.
/// - RAII subscription token automatically unsubscribe on destruction.
///
/// Typical use:
///   auto ev = std::make_shared<Event<std::string>>();
///   auto sub = ev->subscribe([](const std::string& s) { std::puts(s.c_str()); });
///   ev->publish("hello");

template <typename... Ts>
class Event : public std::enable_shared_from_this<Event<Ts...>> {
public:
	using Callback = std::function<void(const Ts&...)>;

	Event()
		: slots_(std::make_shared<SlotVec>())
		, next_id_(1) { }

	Event(const Event&)				= delete;
	Event& operator =(const Event&) = delete;

	class Subscription {
	public:
		Subscription() = default;
		~Subscription() { unsubscribe(); }

		Subscription(Subscription&& other) noexcept 
			: owner_(std::exchange(other.owner_, nullptr)), id_(std::exchange(other.id_, 0))
		{ }

		Subscription& operator =(Subscription&& other) noexcept {
			if (this != &other) {
				unsubscribe();
				owner_ = std::move(other.owner_);
				id_ = other.id_;
				other.id_ = 0;
			}
			return *this;
		}

		Subscription(const Subscription&)				= delete;
		Subscription& operator =(const Subscription&)	= delete;

		bool unsubscribe() {
			auto ev = owner_.lock();
			if (!ev || id_ == 0) return false;
			bool removed = ev->unsubscribe(id_);
			id_ = 0;
			owner_.reset();
			return removed;
		}

		explicit operator bool() const noexcept { return id_ != 0; }

	private:
		friend class Event<Ts...>;
		Subscription(std::weak_ptr<Event> owner, std::size_t id)
			: owner_(std::move(owner)), id_(id) { }
		std::weak_ptr<Event> owner_;
		std::size_t id_ = 0;
	}; // class Subscription

	// Add a subscriber.
	// Returns RAII Token, destroying it unsubscribes.
	//
	[[nodiscard]] Subscription subscribe(Callback cb) {
		const std::size_t id = next_id_.fetch_add(1, std::memory_order_relaxed);

		// Copy-on-write update of the slot vector.
		//
		std::lock_guard<std::mutex> lk(write_mtx_);
		auto curr = slots_.load(std::memory_order_acquire); 
		auto next = std::make_shared<SlotVec>(*curr);
		next->emplace_back(id, std::move(cb));
		slots_.store(std::move(next), std::memory_order_release);

		return Subscription{this->weak_from_this(), id};
	}

	// Manually unsubscribe by ID (normally handled by Subscription)
	//
	bool unsubscribe(std::size_t id) {
		std::lock_guard<std::mutex> lk(write_mtx_);
		auto curr = slots_.load(std::memory_order_acquire);
		auto next = std::make_shared<SlotVec>();
		next->reserve(curr->size());
		bool removed = false;
		// TODO: Is there move efficient way doing this?
		//
		for (auto& [sid, fn] : *curr) {
			if (sid == id) {
				removed = true;
			} else {
				next->emplace_back(sid, fn);
			}
		}

		if (removed) {
			slots_.store(std::move(next), std::memory_order_release);
		}

		return removed;
	}

	// Publish an event to all current subscribers.
	//
	void publish(const Ts&... args) const {
		auto snapshot = slots_.load(std::memory_order_acquire);
		for (auto& slot : *snapshot) {
			try {
				slot.second(args...);
			} catch (...) {
				// TODO: Swallow or route to handler.
			}
		}
	}

	std::size_t subscriber_count() const noexcept {
		auto snapshot = slots_.load(std::memory_order_acquire);
		return snapshot->size();
	}
	
	void clear() {
		std::lock_guard<std::mutex> lk(write_mtx_);
		auto empty = std::make_shared<SlotVec>();
		slots_.store(std::move(empty), std::memory_order_release);
	}

private:
	using Slot =		std::pair<std::size_t, Callback>;
	using SlotVec =		std::vector<Slot>;

	// Snapshot of subscribers, atomically replaced on updates.
	std::atomic<std::shared_ptr<SlotVec>>	slots_;
	std::mutex								write_mtx_; // Protects copy-on-write updates
	std::atomic<std::size_t>				next_id_;

};

template <typename... Ts>
class EventBus {
public:
	using EventType = Event<Ts...>;

	typename EventType::Subscription subscribe(const std::string& topic,
			typename EventType::Callback cb) {
		std::lock_guard<std::mutex> lk(m_);
		auto& ch = channels_[topic];
		if (!ch) {
			ch = std::make_shared<EventType>();
		}
		return ch->subscribe(std::move(cb));
	}

	void publish(const std::string& topic, const Ts& ...args) const {
		std::shared_ptr<EventType> ch;
		{
			std::lock_guard<std::mutex> lk(m_);
			auto it = channels_.find(topic);
			if (it != channels_.end()) {
				ch = it->second;
			}
		}
		if (ch) {
			ch->publish(args...);
		}
	}

	std::size_t subsriber_count(const std::string& topic) const {
		std::lock_guard<std::mutex> lk(m_);
		auto it = channels_.find(topic);
		return it == channels_.end() ? 0 : it->second->subscriber_count();
	}

private:
	mutable std::mutex m_;
	std::unordered_map<std::string, std::shared_ptr<EventType>> channels_;
};

} // namespace stel
