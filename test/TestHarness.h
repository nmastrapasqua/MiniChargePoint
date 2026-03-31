/**
 * TestHarness.h — Harness minimale per unit test senza framework esterno.
 *
 * Fornisce:
 *   - Contatori globali g_passed / g_failed
 *   - runTest(): esegue una funzione test e cattura eccezioni
 *   - Macro ASSERT_EQ, ASSERT_TRUE, ASSERT_FALSE
 *   - Helper writeToTempFile() per test che necessitano di file temporanei
 */
#ifndef TESTHARNESS_H
#define TESTHARNESS_H

#include <iostream>
#include <string>
#include <functional>
#include <stdexcept>
#include <fstream>

#include <Poco/TemporaryFile.h>

// ---------------------------------------------------------------------------
// Contatori globali
// ---------------------------------------------------------------------------
static int g_passed = 0;
static int g_failed = 0;

// ---------------------------------------------------------------------------
// Esecuzione test
// ---------------------------------------------------------------------------
static void runTest(const std::string& name, std::function<void()> fn)
{
    try {
        fn();
        std::cout << "  [PASS] " << name << "\n";
        ++g_passed;
    } catch (const std::exception& ex) {
        std::cerr << "  [FAIL] " << name << " — " << ex.what() << "\n";
        ++g_failed;
    } catch (...) {
        std::cerr << "  [FAIL] " << name << " — unknown exception\n";
        ++g_failed;
    }
}

// ---------------------------------------------------------------------------
// Helper per conversione a stringa (usato dalle macro ASSERT)
// ---------------------------------------------------------------------------
__attribute__((unused))
static std::string std_to_string_helper(const std::string& s) { return s; }
static std::string std_to_string_helper(int v) { return std::to_string(v); }

// ---------------------------------------------------------------------------
// Macro di asserzione
// ---------------------------------------------------------------------------
#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) throw std::runtime_error( \
        std::string(#a " != " #b " → \"") + std_to_string_helper(a) + \
        "\" vs \"" + std_to_string_helper(b) + "\""); } while(0)

#define ASSERT_TRUE(expr) \
    do { if (!(expr)) throw std::runtime_error( \
        std::string("assertion failed: " #expr)); } while(0)

#define ASSERT_FALSE(expr) \
    do { if ((expr)) throw std::runtime_error( \
        std::string("expected false: " #expr)); } while(0)

// ---------------------------------------------------------------------------
// Helper: scrive una stringa in un file temporaneo e restituisce il path
// ---------------------------------------------------------------------------
__attribute__((unused))
static std::string writeToTempFile(const std::string& content)
{
    Poco::TemporaryFile tmpFile;
    std::string path = tmpFile.path();
    tmpFile.keepUntilExit();
    {
        std::ofstream ofs(path);
        ofs << content;
    }
    return path;
}

#endif // TESTHARNESS_H
