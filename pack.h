#include <cstdint>
#include <tuple>
#include <utility>
#include <limits>

namespace pack {
	enum class endian { little, big, native = little };

	class exception : public std::exception {
		std::string message;
		public:
		exception(const std::string& _message) : message(_message) { }
		virtual const char* what() const noexcept {
			return message.c_str();
		}
	};

	namespace {
		template<typename head_type, typename... tail_types> struct follow_up {
			template<typename head_argument, typename... tail_arguments> static std::string pack(const head_argument& data, const tail_arguments&... arguments) {
				return head_type::pack(std::move(data)) + follow_up<tail_types...>::pack(arguments...);
			}
			static std::tuple<typename head_type::decoder, typename tail_types::decoder...> unpack(std::string::const_iterator current, const std::string::const_iterator& end) {
				return tuple_cat(std::make_tuple(head_type::unpack(current, end)), follow_up<tail_types...>::unpack(current, end));
			}
		};
		template<typename head_type> struct follow_up<head_type> {
			template<typename head_argument> static std::string pack(const head_argument& data) {
				return head_type::pack(data);
			}
			static std::tuple<typename head_type::decoder> unpack(std::string::const_iterator current, const std::string::const_iterator& end) {
				return std::make_tuple(head_type::unpack(current, end));
			}
		};

		template<enum endian order> void byte_copy(char* target, const char* begin, const char* end);
		template<> void byte_copy<endian::big>(char* target, const char* begin, const char* end) {
			auto current = end;
			while (--current >= begin) {
				*target++ = *current;
			}
		}

		template<> void byte_copy<endian::native>(char* target, const char* begin, const char* end) {
			auto current = begin;
			while(current < end)
				*target++ = *current++;
		}

		template<enum endian order> void byte_copy(char* target, const std::string::const_iterator& begin, const std::string::const_iterator& end) {
			return byte_copy<order>(target, &*begin, &*end);
		}
	}

	struct piece {
		const std::string::const_iterator begin;
		const std::string::const_iterator end;
		piece(const std::string::const_iterator& _begin, const std::string::const_iterator& _end) : begin(_begin), end(_end) { }
	};

	template<typename raw_type, endian order = endian::native> struct integral {
		struct decoder : public piece {
			using piece::piece;
			raw_type decode() const noexcept {
				raw_type temp(0);
				byte_copy<order>(reinterpret_cast<char*>(&temp), begin, end);
				return temp;
			}
		};
		static std::string pack(raw_type value) noexcept {
			char buffer[sizeof(raw_type)];
			byte_copy<order>(buffer, reinterpret_cast<const char*>(&value), reinterpret_cast<const char*>(&value + 1));
			return std::string(buffer, buffer + sizeof buffer);
		}
		static decoder unpack(std::string::const_iterator& current, const std::string::const_iterator& end) noexcept {
			static const std::string null(sizeof(raw_type), '\0');
			static const decoder null_decoder(null.begin(), null.end());

			if (current + sizeof(raw_type) <= end) {
				auto begin = current;
				current += sizeof(raw_type);
				return decoder(begin, current);
			}
			else {
				return null_decoder;
			}
		}
	};

	template<typename raw_type = unsigned long long, endian order = endian::little> struct compressed;
	template<typename raw_type> struct compressed<raw_type, endian::little> {
		static const size_t block_size = 1 << 7;
		static const size_t mask = block_size - 1;

		struct decoder : public piece {
			using piece::piece;
			raw_type decode() const noexcept {
				raw_type factor = 1, ret = 0;
				auto current = begin;
				while (current < end) {
					ret += (static_cast<unsigned char>(*current++) & mask) * factor;
					factor *= block_size;
				}
				return ret;
			}
		};

		static std::string pack(raw_type value) noexcept {
			std::string ret;
			if (value == 0)
				return std::string("\0", 1);
			while (value) {
				unsigned char current = value % block_size;
				ret.push_back(current | block_size);
				value /= block_size;
			}
			if (ret.size())
				*(ret.end() - 1) &= mask;
			return ret;
		}
		static decoder unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			std::string::const_iterator begin = current;
			while (static_cast<unsigned char>(*current++) & block_size) {
				if (current == end)
					throw exception("Incomplete compressed integer");
			}
			return decoder(begin, current);
		}
	};

	enum class padding { null };

	struct stringer {
		struct decoder : public piece {
			using piece::piece;
			std::string decode() const {
				return std::string(begin, end);
			}
		};
	};

	template<int length, enum padding = padding::null> struct fixed_string : public stringer {
		static std::string pack(std::string value) {
			if (value.size() != length)
				throw exception("Packed string should be of length " + std::to_string(length));
			return value;
		}
		static decoder unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			if (current + length <= end) {
				auto begin = current;
				current += length;
				return decoder(begin, current);
			}
			else
				throw exception("Not enough data left in buffer to unpack fixed_string, expected " + std::to_string(length) + " got " + std::to_string(end - current) );
		}
	};

	template<typename length_encoder = integral<unsigned, endian::little>> struct varchar : public stringer {
		static std::string pack(std::string value) {
			return length_encoder::pack(value.size()) + value;
		}
		static decoder unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			size_t length = length_encoder::unpack(current, end).decode();
			if (unsigned(end - current) >= length) {
				auto begin = current;
				current += length;
				return decoder(begin, current + length);
			}
			else
				throw exception("Not enough data left in buffer to unpack varchar, expected " + std::to_string(length) + " got " + std::to_string(end - current) );
		}
	};

	template<typename... elements> class format {
		typedef follow_up<elements...> packer;

		template<typename decoder> static decltype(auto) decode(const decoder& decoding) {
			return decoding.decode();
		}
		template<typename tuple, size_t... I> static auto decode_all(const tuple& decoders, std::index_sequence<I...> ) {
			return std::make_tuple(decode(std::get<I>(decoders))...);
		}
		public:
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
