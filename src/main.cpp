#include <iostream>
#include <thread>

#include "event.hpp"

using namespace stel;

int main() {

	auto ev = std::make_shared<Event<int>>();

	[[maybe_unused]] auto s1 = ev->subscribe([](int v) { std::cout << "A: " << v << '\n'; });
	[[maybe_unused]] auto s2 = ev->subscribe([](int v) { std::cout << "B: " << v << '\n'; });

	std::thread pub1([&] {
		for (int i = 0; i < 4; i++) {
			ev->publish(i);
		}
	});

	std::thread pub2([&] {
		for (int i = 4; i < 8; i++) {
			ev->publish(i);
		}
	});

	pub1.join();
	pub2.join();

	s1.unsubscribe();
	ev->publish(42); // Only B receives this

	auto eb = std::make_shared<EventBus<std::string, int>>();

	[[maybe_unused]] auto es1 = eb->subscribe("cpu", [](const std::string& s, int v) { 
			std::cout << "[cpu]: EA: " << s << " and number: " << v << '\n';
	});

	[[maybe_unused]] auto es2 = eb->subscribe("cpu", [](const std::string& s, int v) { 
			std::cout << "[cpu]: EB: " << s << " and number: " << v << '\n';
	});

	[[maybe_unused]] auto es3 = eb->subscribe("gpu", [](const std::string& s, int v) { 
			std::cout << "[cpu]: EC: " << s << " and number: " << v << '\n';
	});

	std::thread e_pub1([&] {
		for (int i = 0; i < 4; i++) {
			eb->publish("cpu", "Hello World", i);
			eb->publish("gpu", "XXXX", i);
		}
	});

	std::thread e_pub2([&] {
		for (int i = 4; i < 8; i++) {
			eb->publish("cpu", "oOoOoO", i);
		}
	});

	e_pub1.join();
	e_pub2.join();


    return 0;
}
