#include <cstdint>
#include <tuple>
#include <utility>
#include <limits>

namespace pack {
	template<typename head_type, typename... tail_types> struct follow_up {
		typedef typename head_type::template chain<tail_types...> packer;
		template<typename... argument_types> static std::string pack(const argument_types&... arguments) {
			return packer::pack(arguments...);
		}
		static auto unpack(std::string::const_iterator current, const std::string::const_iterator& end) {
			auto ret = head_type::unpack(current, end);
			return tuple_cat(std::move(ret), follow_up<tail_types...>::unpack(current, end));
		}
	};

	struct finalizer { };

	template<typename head_type> struct follow_up<head_type> {
		typedef typename head_type::template chain<finalizer> packer;
		template<typename... argument_types> static std::string pack(const argument_types&... arguments) {
			return packer::pack(arguments...);
		}
		static auto unpack(std::string::const_iterator current, const std::string::const_iterator& end) {
			return head_type::unpack(current, end);
		}
	};
	template<> struct follow_up<finalizer> {
		static std::string pack() noexcept {
			return std::string();
		}
		template<typename... argument_types> static std::string pack(const argument_types&...) noexcept {
			return std::string();
		}
	};

	template<typename decoder> decltype(auto) decode(const decoder& decoding) {
		return decoding.decode();
	}
	template<typename tuple, size_t... I> auto decode_all(const tuple& decoders, std::index_sequence<I...> ) {
		return std::tuple_cat(decode(std::get<I>(decoders))...);
	}

	struct piece {
		const std::string::const_iterator begin;
		const std::string::const_iterator end;
	};

	enum byte_order { little_endian, big_endian, native_endian = little_endian };

	template<byte_order order> struct byte_orderer {
		template<typename raw_type> raw_type swap(const raw_type& original) {
			return original; // XXX
		}
	};
	template<> struct byte_orderer<native_endian> {
		template<typename raw_type> raw_type swap(const raw_type& original) {
			return original;
		}
	};
	template<byte_order order> void byte_copy(char* target, const char* begin, const char* end) {
		auto current = end;
		while (--current >= begin) {
			*target++ = *current;
		}
	}

	template<> void byte_copy<native_endian>(char* target, const char* begin, const char* end) {
		auto current = begin;
		while(current < end)
			*target++ = *current++;
	}

	template<byte_order order> void byte_copy(char* target, const std::string::const_iterator& begin, const std::string::const_iterator& end) {
		return byte_copy<order>(target, &*begin, &*end);
	}

	template<typename raw_type, byte_order order = native_endian> struct integral {
		struct decoder : public piece {
			decoder(const std::string::const_iterator& _begin, const std::string::const_iterator& _end) noexcept : piece{_begin, _end} {
			}
			auto decode() const noexcept {
				raw_type temp(0);
				byte_copy<order>(reinterpret_cast<char*>(&temp), begin, end);
				return std::make_tuple(temp);
			}
		};
		template<typename... followers> struct chain {
			template<typename... argument_types> static std::string pack(raw_type value, const argument_types&... arguments) noexcept {
				char buffer[sizeof(raw_type)];
				byte_copy<order>(buffer, reinterpret_cast<const char*>(&value), reinterpret_cast<const char*>(&value + 1));
				return std::string(buffer, buffer + sizeof buffer) + follow_up<followers..., argument_types...>::pack(arguments...);
			}
		};
		static auto unpack(std::string::const_iterator& current, const std::string::const_iterator& end) noexcept {
			static const std::string null(sizeof(raw_type), '\0');
			static const decoder null_decoder(null.begin(), null.end());

			if (current + sizeof(raw_type) <= end) {
				auto begin = current;
				current += sizeof(raw_type);
				return std::make_tuple(decoder(begin, end));
			}
			else {
				throw "WHAT!";
				return std::make_tuple(null_decoder);
			}
		}
	};

	enum padding_type { padding_null };

	template<padding_type = padding_null> class varchar {
	};

	template<typename... elements> struct format {
		typedef follow_up<elements...> packer;
		template<typename... argument_types> static std::string pack(const argument_types&... arguments) {
			return packer::pack(arguments...);
		}
		static auto unpack(const std::string& packed) {
			std::string::const_iterator begin = packed.begin();
			auto plan = packer::unpack(begin, packed.end());
			using Indices = std::make_index_sequence<std::tuple_size<std::decay_t<decltype(plan)>>::value>;
			return decode_all(plan, Indices{});
		}

	};
}
