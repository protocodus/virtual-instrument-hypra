// Local analysis tool (not part of gearmulator): boots the JP-8000, then runs a
// script of "wait S", "patch PP VV" (upper patch byte), "perf PP VV" (common),
// "on N V", "off N", "dump NAME". dump writes every ESP's program words.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include "jeLib/device.h"
#include "jeLib/je8086.h"
#include "jeLib/romloader.h"

using namespace jeLib;

int main(int argc, char* argv[])
{
	if (argc < 3) { std::cerr << "jeProbe script outdir\n"; return 1; }
	auto rom = RomLoader::findROM();
	if (!rom.isValid()) { std::cerr << "no rom\n"; return 1; }
	synthLib::DeviceCreateParams params;
	params.romData = rom.getData();
	params.romName = rom.getName();
	params.hostSamplerate = 88200;
	params.preferredSamplerate = 88200;
	Device device(params);
	synthLib::TAudioInputs inputs{};
	synthLib::TAudioOutputs outputs{};
	std::array<std::vector<float>, 2> out;
	for (size_t i = 0; i < 2; ++i) { out[i].resize(128); outputs[i] = out[i].data(); }
	std::vector<synthLib::SMidiEvent> midiIn, midiOut;
	const auto run = [&](double seconds)
	{
		for (uint64_t n = 0; n < uint64_t(seconds * 88200 / 128); ++n)
		{
			device.process(inputs, outputs, 128, midiIn, midiOut);
			midiIn.clear(); midiOut.clear();
		}
	};
	const auto sysex = [&](uint32_t addr, uint8_t v)
	{
		synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
		std::vector<uint8_t> s { 0xf0, 0x41, 0x10, 0x00, 0x06, 0x12 };
		const uint8_t a[4] { uint8_t((addr >> 24) & 0x7f), uint8_t((addr >> 16) & 0x7f), uint8_t((addr >> 8) & 0x7f), uint8_t(addr & 0x7f) };
		int sum = 0;
		for (auto b : a) { s.push_back(b); sum += b; }
		s.push_back(v); sum += v;
		s.push_back(uint8_t((128 - (sum & 127)) & 127));
		s.push_back(0xf7);
		e.sysex = s;
		midiIn.push_back(e);
	};
	const auto shortMsg = [&](uint8_t a, uint8_t b, uint8_t c)
	{
		synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
		e.a = a; e.b = b; e.c = c;
		midiIn.push_back(e);
	};
	std::ifstream script(argv[1]);
	std::string line, outdir = argv[2];
	while (std::getline(script, line))
	{
		std::istringstream w(line);
		std::string cmd;
		w >> cmd;
		if (cmd == "wait") { double s; w >> s; run(s); }
		else if (cmd == "patch") { unsigned p, v; w >> std::hex >> p >> std::dec >> v; sysex(0x01004000u | p, uint8_t(v)); run(0.02); }
		else if (cmd == "perf") { unsigned p, v; w >> std::hex >> p >> std::dec >> v; sysex(0x01000000u | p, uint8_t(v)); run(0.02); }
		else if (cmd == "on") { unsigned n, v; w >> n >> v; shortMsg(0x90, uint8_t(n), uint8_t(v)); run(0.005); }
		else if (cmd == "off") { unsigned n; w >> n; shortMsg(0x80, uint8_t(n), 0); run(0.005); }
		else if (cmd == "dump")
		{
			std::string name; w >> name;
			std::ofstream f(outdir + "/" + name + ".bin", std::ios::binary);
			device.getJe8086().forEachEsp([&](const auto& esp)
			{
				for (unsigned core = 0; core < 2; ++core)
					f.write(reinterpret_cast<const char*>(esp.interpreter().wordsOf(core)), 1024 * 4);
			});
		}
	}
	return 0;
}
