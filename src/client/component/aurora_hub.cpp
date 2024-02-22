#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "aurora_hub.hpp"
#include "console.hpp"
#include "motd.hpp"

#include <utils/http.hpp>
#include <utils/io.hpp>
#include <utils/string.hpp>

#include <algorithm>

namespace aurora_hub
{
	namespace
	{

#define FIND_START_OF_TAG(_index_start, _index_add, _name, _variable) \
	for (index = _index_start; index < length_string; ++index) \
	{ \
		if (result_buffer[index] == _name) \
		{ \
			_variable = index + _index_add; \
			break; \
		} \
	}

#define CLEAR_EMPTY_SPACE \
	while (result_buffer[start] == ' ') \
	{ \
		result_buffer[start] = '\0'; \
		++start; \
	}

		void parse_html_code_in_response(ui_scripting::table* content, const utils::http::result& result)
		{
			auto result_buffer = result.buffer.data();
			int length_string = std::strlen(result_buffer);
			size_t start = 0, end = 0;
			int index = 0;

			// <a></a> tag contains map_name
			start = result.buffer.find_first_of("<a href=\"");
			while (start != std::string::npos)
			{
				end = result.buffer.find_first_of("/\">");
				if (end == std::string::npos)
				{
					start = result.buffer.find_first_of("<a href=\"", start + 1);
					continue;
				}

				FIND_START_OF_TAG(start, 1, '/', end)
				auto map_name = result.buffer.substr(start, end);

				// outside of </a> tag to `-` has date information with timestamp
				FIND_START_OF_TAG(end, 1, '/', start)
				FIND_START_OF_TAG(start, 0, '-', end)
				FIND_START_OF_TAG(end, 0, ' ', start)
				auto map_date = result.buffer.substr(start, end);

				console::debug("adding %s with date '%s'\n", map_name.data(), map_date.data());
				content->set(map_name, map_date);

				start = result.buffer.find_first_of("<a href=\"", start + 1);
			}
		}
	}

	ui_scripting::table get_available_content()
	{
		ui_scripting::table featured_content{};

		const auto url = utils::string::va(CUSTOM_MAPS_CDN);
		const auto result = utils::http::get_data(url, {}, {}, {});
		if (result.has_value())
		{
			parse_html_code_in_response(&featured_content, result.value());
		}

		return featured_content;
	}

	class component final : public component_interface
	{
	public:

		void post_unpack() override
		{
			// TODO
		}
	};
}

REGISTER_COMPONENT(aurora_hub::component)