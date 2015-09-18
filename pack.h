#include <cstdint>

namespace pack {
	template<typename head_type, typename... tail_types> struct follow_up {
		template<typename... argument_types> static std::string pack(const argument_types&... arguments) {
			typedef typename head_type::template chain<tail_types...> packer;
			return packer::pack(arguments...);
		}
	};
	struct finalizer { };
	template<typename head_type> struct follow_up<head_type> {
		template<typename... argument_types> static std::string pack(const argument_types&... arguments) {
			typedef typename head_type::template chain<finalizer> packer;
			return packer::pack(arguments...);
		}
	};
	template<> struct follow_up<finalizer> {
		template<typename... argument_types> static std::string pack(const argument_types&...) {
			//static_assert(false, "Too many arguments to pack");
			return std::string();
		}
		static std::string pack() {
			return std::string();
		}
	};
	template<typename raw_type> struct integral {
		template<typename... followers> struct chain {
			template<typename... argument_types> static std::string pack(raw_type value, const argument_types&... arguments) {
				return std::string(reinterpret_cast<char*>(&value), reinterpret_cast<char*>(&value + 1)) + follow_up<followers..., argument_types...>::pack(arguments...);
			}
		};
	};

	template<typename... elements> struct format {
		template<typename... argument_types> static std::string pack(const argument_types&... arguments) {
			return follow_up<elements...>::pack(arguments...);
		}
	};
}
