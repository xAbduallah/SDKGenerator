// Unreal Engine SDK Generator
// by KN4CK3R
// https://www.oldschoolhack.me


#include <fstream>
#include <unordered_set>
#include <unordered_map>
#include <chrono>
#include "Tools.h"
#include "cpplinq.hpp"
#include "Logger.hpp"
#include "includes.h"
#include "IGenerator.hpp"
#include "ObjectsStore.hpp"
#include "NamesStore.hpp"
#include "Package.hpp"
#include "NameValidator.hpp"
#include "PrintHelper.hpp"

#include "Main.h"

extern IGenerator* generator;

/// <summary>
/// Dumps the objects and names to files.
/// </summary>
/// <param name="path">The path where to create the dumps.</param>
void Dump(std::string path)
{
	{
		std::ofstream o(path + "/" + "NamesDump.txt");
		tfm::format(o, "Address: %P\n\n", NamesStore::GetAddress());
		for (auto name : NamesStore())
		{
			tfm::format(o, "[%06i] %s\n", name.Index, name.NamePrivate);
		}
	}

	{
		std::ofstream o(path + "/" + "ObjectsDump.txt");
		tfm::format(o, "Address: %P\n\n", ObjectsStore::GetAddress());
		for (auto obj : ObjectsStore())
		{

			tfm::format(o, "[%06i] %-100s 0x%P\n", obj.GetIndex(), obj.GetFullName(), obj.GetAddress());
		}
	}
}



void DumpCoreUObjectSizes(std::string path)
{
	UEObject corePackage;

	for (auto obj : ObjectsStore())
	{
		const auto package = obj.GetPackageObject();

		if (package.IsValid() && package.GetFullName() == "Package CoreUObject")
		{
			corePackage = package;
		}
	}

	std::ofstream o(path + "/" + "CoreUObjectInfo.txt");

	if (!corePackage.IsValid()) return;

	for (auto obj : ObjectsStore())
	{
		if (obj.GetPackageObject() == corePackage && obj.IsA<UEClass>())
		{
			auto uclass = obj.Cast<UEClass>();

			if (obj.GetName().find("Default__") == 0) continue;

			tfm::format(o, "class %-35s", uclass.GetNameCPP());

			auto superclass = uclass.GetSuper();

			if (superclass.IsValid())
			{
				tfm::format(o, ": public %-25s ", superclass.GetNameCPP());

				tfm::format(o, "// 0x%02X (0x%02X - 0x%02X) \n", uclass.GetPropertySize() - superclass.GetPropertySize(), superclass.GetPropertySize(), uclass.GetPropertySize());
			}
			else
			{
				tfm::format(o, "%-35s// 0x%02X (0x00 - 0x%02X) \n", "", uclass.GetPropertySize(), uclass.GetPropertySize());
			}
		}
	}


}

/// <summary>
/// Generates the sdk header.
/// </summary>
/// <param name="path">The path where to create the sdk header.</param>
/// <param name="processedObjects">The list of processed objects.</param>
/// <param name="packageOrder">The package order info.</param>

void SaveSDKHeader(std::string path, const std::unordered_map<UEObject, bool>& processedObjects, const std::vector<std::unique_ptr<Package>>& packages)
{
	std::ofstream os(path + "/" +  "SDK.hpp");


	os << "#pragma once\n\n";
	os << "// SDKGen by @XNinja_Leaks | @talhaeens\n";

	//Includes
	os << "#include <set>\n";
	os << "#include <string>\n";
	for (auto&& i : generator->GetIncludes())
	{
		os << "#include " << i << "\n";
	}

	{
		{
			std::ofstream os2(path + "/SDK" + "/" + tfm::format("%s_Basic.hpp", generator->GetGameNameShort()));
			std::vector<std::string> incs = {
					"<iostream>",
					"<string>",
					"<unordered_set>",
					"<codecvt>"
			};
			PrintFileHeader(os2, incs, true);

			os2 << generator->GetBasicDeclarations() << "\n";

			PrintFileFooter(os2);

			os << "\n#include \"SDK/" << tfm::format("%s_Basic.hpp", generator->GetGameNameShort()) << "\"\n";
		}
		{
			std::ofstream os2(path + "/SDK" +  "/" +  tfm::format("%s_Basic.cpp", generator->GetGameNameShort()));

			PrintFileHeader(os2, { "\"../SDK.hpp\"" }, false);

			os2 << generator->GetBasicDefinitions() << "\n";

			PrintFileFooter(os2);
		}
	}

	using namespace cpplinq;

	//check for missing structs
	const auto missing = from(processedObjects) >> where([](auto&& kv) { return kv.second == false; });
	if (missing >> any())
	{
		std::ofstream os2(path + "/SDK" + "/" + tfm::format("%s_MISSING.hpp", generator->GetGameNameShort()));

		PrintFileHeader(os2, true);

		for (auto&& s : missing >> select([](auto&& kv) { return kv.first.template Cast<UEStruct>(); }) >> experimental::container())
		{
			os2 << "// " << s.GetFullName() << "\n// ";
			os2 << tfm::format("0x%04X\n", s.GetPropertySize());

			os2 << "struct " << MakeValidName(s.GetNameCPP()) << "\n{\n";
			os2 << "\tunsigned char UnknownData[0x" << tfm::format("%X", s.GetPropertySize()) << "];\n};\n\n";
		}

		PrintFileFooter(os2);

		os << "\n#include \"SDK/" << tfm::format("%s_MISSING.hpp", generator->GetGameNameShort()) << "\"\n";
	}

	os << "\n";

	for (auto&& package : packages)
	{
		os << R"(#include "SDK/)" << GenerateFileName(FileContentType::Structs, *package) << "\"\n";
		os << R"(#include "SDK/)" << GenerateFileName(FileContentType::Classes, *package) << "\"\n";
		if (generator->ShouldGenerateFunctionParametersFile())
		{
			os << R"(#include "SDK/)" << GenerateFileName(FileContentType::FunctionParameters, *package) << "\"\n";
		}
	}
}

/// <summary>
/// Process the packages.
/// </summary>
/// <param name="path">The path where to create the package files.</param>
void ProcessPackages(std::string path)
{
	using namespace cpplinq;

	const auto sdkPath = path + "/SDK";
	mkdir(sdkPath.c_str(), 0777);

	std::vector<std::unique_ptr<Package>> packages;

	std::unordered_map<UEObject, bool> processedObjects;

	auto packageObjects = from(ObjectsStore())
			>> select([](auto&& o) { return o.GetPackageObject(); })
			>> where([](auto&& o) { return o.IsValid(); })
			>> distinct()
			>> to_vector();

	std::ofstream fndmp(path + "/" + "FunctionsDump.txt", std::ostream::out | std::ostream::trunc);
	fndmp.close();

	for (auto obj : packageObjects)
	{
		auto package = std::make_unique<Package>(obj);

		package->Process(processedObjects);
		//	package->DumpFunctions(path);
		if (package->Save(sdkPath))
		{
			Package::PackageMap[obj] = package.get();

			packages.emplace_back(std::move(package));
		}
	}

	if (!packages.empty())
	{

		const PackageDependencyComparer comparer;
		for (auto i = 0u; i < packages.size() - 1; ++i)
		{
			for (auto j = 0u; j < packages.size() - i - 1; ++j)
			{
				if (!comparer(packages[j], packages[j + 1]))
				{
					std::swap(packages[j], packages[j + 1]);
				}
			}
		}
	}

	SaveSDKHeader(path, processedObjects, packages);
}

void* main_thread (void *)
{
	LOGI("Attaching Dumper...");

sleep(7);
	uintptr_t UE4 = Tools::GetBaseAddress("libUE4.so");
	LOGI("UE4: %lu", (unsigned long)UE4);
	while (!UE4) {
		UE4 = Tools::GetBaseAddress("libUE4.so");
		sleep(1);
	}
	LOGI("libUE4 Base Address; %zu",UE4);

	if (!ObjectsStore::Initialize())
	{
		return 0;
	}
	LOGI("ObjectsStore::Initialized...");
	if (!NamesStore::Initialize())
	{
		return 0;
	}
	LOGI("NamesStore::Initialized...");
	if (!generator->Initialize())
	{
		return 0;
	}
	LOGI("Generator::Initialized...");



	
	


	std::string outputDirectory = generator->GetOutputDirectory();

	outputDirectory += "/" + generator->GetGameNameShort() + "_(v" + generator->GetGameVersion() + ")_32Bit";


	mkdir(outputDirectory.c_str(), 0777);
	LOGI("Directories Created...");
	LOGI("Dumping Offsets...");

	std::ofstream log(outputDirectory + "/Offsets_Dump.txt");

	auto ProcessEvent_Offset = Tools::FindPattern("libUE4.so", "F0 4B 2D E9 ?? B0 8D E2 ?? D0 4D E2 00 50 A0 E1 ?? ?? ?? ?? 01 70 A0 E1 ?? ?? ?? ?? 02 90 A0 E1");
	if (ProcessEvent_Offset) {
		Logger::SetStream(&log);
		Logger::Log("#define ProcessEvent_Offset: 0x%p", ProcessEvent_Offset - UE4);
	} else {
		Logger::Log("Failed top search ProcessEvent_Offset!");
	}

	auto GWorld_Offset = Tools::FindPattern("libUE4.so", "?? ?? ?? E5 00 60 8F E0 ?? ?? ?? E5 04 00 80 E0");
	if (GWorld_Offset) {
		GWorld_Offset += *(uintptr_t *) ((GWorld_Offset + *(uint8_t *) (GWorld_Offset) + 0x8)) + 0x18;
		Logger::Log("#define GWorld_Offset: 0x%p", GWorld_Offset - UE4);
	} else {
		Logger::Log("Failed to search GWorld pattern!");
	}

	auto GUObjectArray_Offset = Tools::FindPattern("libUE4.so", "?? ?? ?? E5 1F 01 C2 E7 04 00 84 E5 00 20 A0 E3");
	if (GUObjectArray_Offset) {
		GUObjectArray_Offset += *(uintptr_t *) ((GUObjectArray_Offset + *(uint8_t *) (GUObjectArray_Offset) + 0x8)) + 0x18;
		Logger::Log("#define GUObjectArray_Offset: 0x%p", GUObjectArray_Offset - UE4);
	} else {
		Logger::Log("Failed to search GUObjectArray pattern!");
	}


	auto GNames_Offset = Tools::FindPattern("libUE4.so", "E0 01 9F E5 00 00 8F E0 30 70 90 E5");
	if (GNames_Offset) {
	Logger::Log("#define GNames_Offset: 0x%p", GNames_Offset - 0x1F8 - UE4);
	} else {
		Logger::Log("Failed to search GNames pattern!");
	}

	auto GNative_Offset = Tools::FindPattern("libUE4.so", "20 01 00 00 9B 03 00 00 24 03 00 00");
	if (GNative_Offset) {
		Logger::Log("#define GNativeAndroidApp_Offset: 0x%p", GNative_Offset - 0x74D4 - UE4);
	} else {
		Logger::Log("Failed to search GNative_OffsetAndroidApp pattern!");
	}

	auto CanvasMap_Offset = Tools::FindPattern("libUE4.so", "?? ?? ?? E5 24 10 4B E2 18 40 0B E5 00 20 A0 E3");
	if (CanvasMap_Offset) {
		CanvasMap_Offset += *(uintptr_t *) ((CanvasMap_Offset + *(uint8_t *) (CanvasMap_Offset) + 0x8)) + 0x1C;
		Logger::Log("#define CanvasMap_Offset: 0x%p", CanvasMap_Offset - UE4);
	} else {
		Logger::Log("Failed to search CanvasMap pattern!");
	}

	Logger::Log(" ");
	Logger::Log(" ");
	Logger::Log("// SDK and Offsets Gen by @XNinja_Leaks | @talhaeens ");

	std::ofstream log2(outputDirectory + "/Generator.log");
	Logger::SetStream(&log2);

	Logger::Log("Cheking LOGs");

	Logger::Log("Dumping GNames & GObjects...");
	LOGI("Dumping GNames & GObjects...");
	Dump(outputDirectory);

	Logger::Log("Dumping CoreUobject Infos...");
	LOGI("Dumping CoreUobject Infos...");
	DumpCoreUObjectSizes(outputDirectory);

	const auto begin = std::chrono::system_clock::now();

	Logger::Log("Dumping SDK...");
	LOGI("Dumping SDK...");
	ProcessPackages(outputDirectory);

	Logger::Log("Finished, took %d seconds.", std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - begin).count());


	return 0;
}

extern "C"
JNIEXPORT int JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    
     Media_Folder = "/storage/emulated/0/Android/media/" + pkgName;
    
    struct stat info;
    if( stat( Media_Folder.c_str(), &info ) != 0 ){
    LOGE( "cannot access %s\n", Media_Folder.c_str() );
    mkdir(Media_Folder.c_str(), 0777);
    }

   pthread_t m_thread;
   pthread_create(&m_thread, 0, main_thread, 0);
    
   return JNI_VERSION_1_6;
}

extern "C"
{
void __attribute__ ((visibility ("default"))) OnLoad() 
{
    
    Media_Folder = "/storage/emulated/0/Android/media/" + pkgName;
    
    struct stat info;
    if( stat( Media_Folder.c_str(), &info ) != 0 ){
    LOGE( "cannot access %s\n", Media_Folder.c_str() );
    mkdir(Media_Folder.c_str(), 0777);
    }
    
      pthread_t thread;
      pthread_create(&thread, 0, main_thread, 0);
      
}
}