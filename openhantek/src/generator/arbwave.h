// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <memory>
#include <string>
#include <vector>

/// \brief Arbitrary waveforms for the PSG9080 (no Qt, unit tested): formulas, files and the 14-bit format.
///
/// A waveform is one period of 8192 points. Values are worked on as doubles (volts or any unit) and converted to
/// the 14-bit codes of the device (0..16383, 8192 = middle) at the end.
namespace arbwave {

constexpr int kPoints = 8192;
constexpr int kMaxCode = 16383;

/// \brief Formula in one variable, e.g. "sin(2*pi*t) + 0.3*sin(6*pi*t)".
///
/// Variables: t (0..1 over the period, last point excluded), x (= 2*pi*t), n (point index), N (points).
/// Constants: pi, e. Operators: + - * / % ^, comparisons < <= > >= == != (1 or 0), unary -, parentheses.
/// Functions: sin cos tan asin acos atan sinh cosh tanh exp ln log log10 sqrt abs sign floor ceil round frac,
/// sinc(x) = sin(x)/x, square(x) / tri(x) / saw(x) (period 2*pi, -1..1), pulse(x, duty) (period 2*pi, 0/1),
/// gauss(z) = exp(-z^2/2), noise() (deterministic, -1..1), min(a,b) max(a,b) pow(a,b) mod(a,b) atan2(y,x)
/// and if(c, a, b).
class Expression {
  public:
    Expression();
    ~Expression();
    Expression(Expression &&) noexcept;
    Expression &operator=(Expression &&) noexcept;

    /// Compile the text. On failure returns false and error() says where.
    bool parse(const std::string &text);
    const std::string &error() const { return message; }
    /// Value for the variables of point n (of `count`).
    double evaluate(int n, int count) const;

    struct Node;

  private:
    std::unique_ptr<Node> root;
    std::string message;
};

/// Sample a formula over `count` points. False if it does not compile or gives a value that is not finite.
bool sampleFormula(const std::string &formula, std::vector<double> &out, std::string &error, int count = kPoints);

/// Linear resampling of one period to `count` points (keeps the shape, the last point is not repeated).
std::vector<double> resample(const std::vector<double> &in, int count = kPoints);

/// How doubles become codes.
enum class Scaling {
    Normalize, ///< minimum..maximum -> 0..16383 (uses the full resolution; the amplitude is set on the channel)
    Fixed,     ///< -1..+1 -> 0..16383 (clipped), keeps relative levels between waveforms
};
std::vector<int> toCodes(const std::vector<double> &values, Scaling scaling);
/// Codes -> -1..+1.
std::vector<double> fromCodes(const std::vector<int> &codes);

/// \brief Numbers read from a text or CSV file, by column.
///
/// Accepts one value per line, or columns separated by ';', tab, space or ',' (with ';' the decimal separator
/// may be a comma, as in the OpenHantek CSV files). Lines starting with '#' and header lines are skipped.
struct Table {
    std::vector<std::string> header;          ///< names of the columns, if the file had a header line
    std::vector<std::vector<double>> columns; ///< same length for every column
    bool openHantekLog = false;               ///< waveform log of OpenHantek: "aquisicao;data_hora;t (s);valor"
};
bool parseTable(const std::string &text, Table &table, std::string &error);

/// Values of an OpenHantek waveform log for one acquisition (1 = first).
std::vector<double> openHantekAcquisition(const Table &table, int acquisition);
/// Number of acquisitions in an OpenHantek waveform log.
int openHantekAcquisitions(const Table &table);

/// True if `values` are exactly 8192 integers in 0..16383 (a file in the device format, as PSG9080_ARB writes).
bool isDeviceFormat(const std::vector<double> &values);
/// True if `values` are 8192 integers in 0..65535 with some above 16383 (the original software's 16-bit files).
bool isSixteenBitFormat(const std::vector<double> &values);

/// File in the device format: 8192 lines with the 14-bit codes (readable by PSG9080_ARB).
std::string deviceFormatText(const std::vector<int> &codes);

/// Ready-made shapes for the editor: name and formula.
struct Example {
    const char *name;
    const char *formula;
};
const std::vector<Example> &examples();

} // namespace arbwave
