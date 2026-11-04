#pragma once

// Protocol diagnostics: the latest 256 events stay in memory for explicit UI export.
// Optional continuous disk tracing records every event for hardware analysis.
//
// Continuous disk tracing is off unless AURAL_TRACE is set when the process starts:
//   AURAL_TRACE=1        writes to aural-trace.log in the temp directory (%TEMP% on Windows)
//   AURAL_TRACE=<path>   writes to that file
// Each process appends a "trace started" line, then one line per event, prefixed with the
// milliseconds since the trace started.
//
// Header-only so every build of the engine (Qt, ImGui, tests, macOS, Linux) picks it up without
// a build-file change.

#include "CommandSerializer.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <deque>

namespace ProtocolTrace
{
	namespace detail
	{
		inline std::string env(const char* name)
		{
#ifdef _MSC_VER
			char* value = nullptr;
			size_t length = 0;
			if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
				return std::string();
			std::string result(value);
			std::free(value);
			return result;
#else
			const char* value = std::getenv(name);
			return value ? std::string(value) : std::string();
#endif
		}

		struct Sink
		{
			std::mutex mtx;
			std::ofstream out;
            std::deque<std::string> recent;
			std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

			Sink()
			{
				std::string path = env("AURAL_TRACE");
				if (path.empty() || path == "0")
					return;
				if (path == "1")
				{
#ifdef _WIN32
					std::string dir = env("TEMP");
					path = (dir.empty() ? std::string(".") : dir) + "\\aural-trace.log";
#else
					std::string dir = env("TMPDIR");
					path = (dir.empty() ? std::string("/tmp") : dir) + "/aural-trace.log";
#endif
				}
				out.open(path, std::ios::out | std::ios::app);
				if (out.is_open())
					out << "---- trace started ----\n" << std::flush;
			}
		};

		inline Sink& sink()
		{
			static Sink s;
			return s;
		}
	}

	inline bool enabled()
	{
		return detail::sink().out.is_open();
	}

	// Never throws: a trace that fails must not take the session down with it.
	inline void line(const std::string& text) noexcept
	{
		try
		{
			detail::Sink& s = detail::sink();

			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - s.start).count();
			std::lock_guard<std::mutex> guard(s.mtx);
			const std::string entry = std::to_string(ms) + " ms  " + text;
            s.recent.push_back(entry.substr(0, 8192));
            if (s.recent.size() > 256) s.recent.pop_front();
            if (s.out.is_open()) s.out << entry << '\n' << std::flush;
		}
		catch (...)
		{
		}
	}

    inline std::string recentLog()
    {
        auto& s = detail::sink();
        std::lock_guard guard(s.mtx);
        std::string result;
        for (const auto& entry : s.recent) result += entry + "\n";
        return result;
    }

	inline std::string hex(const Buffer& bytes)
	{
		static const char* digits = "0123456789ABCDEF";
		std::string out;
		for (size_t i = 0; i < bytes.size(); ++i)
		{
			if (i)
				out.push_back(' ');
			const unsigned char b = static_cast<unsigned char>(bytes[i]);
			out.push_back(digits[b >> 4]);
			out.push_back(digits[b & 0x0F]);
		}
		return out;
	}

	// T1 = table 1 (DATA_MDR), T2 = table 2 (DATA_MDR_NO2).
	inline std::string table(DATA_TYPE type)
	{
		switch (type)
		{
		case DATA_TYPE::ACK:          return "ACK";
		case DATA_TYPE::DATA_MDR:     return "T1";
		case DATA_TYPE::DATA_MDR_NO2: return "T2";
		default:                      break;
		}
		return "type " + hex(Buffer{ static_cast<char>(type) });
	}

	inline void frame(const char* direction, DATA_TYPE type, unsigned int seq, const Buffer& payload) noexcept
	{

		try
		{
			std::string text = std::string(direction) + " " + table(type) + " seq " + std::to_string(seq);
			if (!payload.empty())
				text += "  " + hex(payload);
			line(text);
		}
		catch (...)
		{
		}
	}

	// A reply that reached the dispatcher but that no decoder accepted: the device said something
	// the engine does not (or not yet) understand, so it changed nothing.
	inline void unused(const CommandSerializer::Message& msg) noexcept
	{

		try
		{
			line("   ^ not used: no decoder accepted this " + table(msg.dataType) + " reply");
		}
		catch (...)
		{
		}
	}
}
