#include "air_compiler.h"

#include <iostream>
#include <spawn.h>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdio>

std::vector<uint8_t> AirCompiler::compile(const std::string& shaderSource) {
    // First, generate AIR from shader source
    std::string inputFile = ".metal";
    int tmpFD = makeTemporaryFile(inputFile);
    write(tmpFD, shaderSource.data(), shaderSource.size());
    close(tmpFD);

    std::string irFile = ".ir";
    tmpFD = makeTemporaryFile(irFile);
    close(tmpFD);

    pid_t pid;
    char* airArgv[] = { "xcrun", "-sdk", "macosx", "metal", "-o", irFile.data(), "-c", inputFile.data(), "-D__air__", "-DUNLEASHED_RECOMP", "-Wno-unused-variable", "-frecord-sources", "-gline-tables-only", nullptr };

    if (posix_spawn(&pid, "/usr/bin/xcrun", nullptr, nullptr, airArgv, nullptr) != 0) {
        unlink(inputFile.data());
        unlink(irFile.data());
        fmt::println("Failed to spawn AIR xcrun process");
        exit(1);
    }

    int status;

    if (waitpid(pid, &status, 0) == -1) {
        unlink(inputFile.data());
        unlink(irFile.data());
        fmt::println("Failed to wait AIR xcrun process");
        exit(1);
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        unlink(inputFile.data());
        unlink(irFile.data());
        fmt::println("AIR xcrun exited with code {}", WEXITSTATUS(status));
        fmt::println("{}", shaderSource);
        exit(1);
    }

    unlink(inputFile.data());

    // Now we need to turn the AIR into a .metallib
    std::string libFile = ".metallib";
    tmpFD = makeTemporaryFile(libFile);
    close(tmpFD);

    char* libArgv[] = { "xcrun", "-sdk", "macosx", "metallib", "-o", libFile.data(), irFile.data(), nullptr };

    if (posix_spawn(&pid, "/usr/bin/xcrun", nullptr, nullptr, libArgv, nullptr) != 0) {
        unlink(irFile.data());
        unlink(libFile.data());
        fmt::println("Failed to spawn .metallib xcrun process");
        exit(1);
    }

    if (waitpid(pid, &status, 0) == -1) {
        unlink(irFile.data());
        unlink(libFile.data());
        fmt::println("Failed to wait .metallib xcrun process");
        exit(1);
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        unlink(irFile.data());
        unlink(libFile.data());
        fmt::println(".metallib exited with code {}", WEXITSTATUS(status));
        exit(1);
    }

    // Read from built .metallib
    FILE* file = fopen(libFile.data(), "rb");
    fseek(file, 0, SEEK_END);
    size_t fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    auto data = std::vector<uint8_t>(fileSize);
    fread(data.data(), 1, fileSize, file);
    fclose(file);

    // Cleanup temporary files
    unlink(irFile.data());
    unlink(libFile.data());

    return data;
}

int AirCompiler::makeTemporaryFile(std::string &extension) {
    const std::string path = "/tmp/xenos_metal_XXXXXX";

    size_t size = path.size() + extension.size() + 1;
    char fullTemplate[size];
    snprintf(fullTemplate, size, "%s%s", path.c_str(), extension.c_str());

    int tmpFD = mkstemps(fullTemplate, extension.size());
    if (tmpFD == -1) {
        fmt::println("Failed to open temporary file, \"{}\"!", std::string(fullTemplate));
        unlink(fullTemplate);
        exit(1);
    }

    extension = fullTemplate;
    return tmpFD;
}
