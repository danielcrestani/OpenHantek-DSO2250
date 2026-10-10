// SPDX-License-Identifier: GPL-2.0+

#include "arbwave.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace arbwave {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

double phaseOf(double x) { // 0..1 within a 2*pi period
    const double p = x / kTwoPi;
    return p - std::floor(p);
}

/// Number at the start of `s` with a decimal point, whatever the C locale is (strtod would follow LC_NUMERIC,
/// which QApplication sets from the system: pt_BR uses a comma). Returns the characters used, 0 if none.
size_t readNumber(const char *s, size_t len, double &value) {
    size_t skip = (len > 0 && s[0] == '+') ? 1 : 0;
    const auto r = std::from_chars(s + skip, s + len, value);
    if (r.ec != std::errc()) return 0;
    return (size_t)(r.ptr - s);
}

double noiseAt(int n) { // deterministic: the same formula always gives the same waveform
    uint32_t v = (uint32_t)n * 2654435761u + 0x9e3779b9u;
    v ^= v >> 16;
    v *= 0x85ebca6bu;
    v ^= v >> 13;
    v *= 0xc2b2ae35u;
    v ^= v >> 16;
    return v / 2147483647.5 - 1.0;
}

/// Independent noise sources: source k at index i (deterministic).
double noiseSource(int k, int i) { return noiseAt(i * 31 + k * 1000003 + 7); }

/// Pink (1/f, -3 dB/octave) by the Voss-McCartney method: source k changes every 2^k points, so the sum can be
/// computed for each point alone. 13 octaves cover the 8192 points of a period.
double pinkAt(int n) {
    double sum = noiseAt(n);
    for (int k = 1; k <= 13; ++k) sum += noiseSource(k, n >> k);
    return sum / 4.2; // about the same RMS as the uniform white noise
}

/// Brown (1/f^2, -6 dB/octave): the same sources, weighted by 2^(k/2) (each octave twice the power below).
double brownAt(int n) {
    double sum = 0, weights = 0;
    for (int k = 0; k <= 13; ++k) {
        const double w = std::pow(2.0, k / 2.0);
        sum += w * noiseSource(k, n >> k);
        weights += w * w;
    }
    return sum / std::sqrt(weights) * 0.58;
}

/// Gaussian-like white noise (sum of 4 uniform values): crest factor about 3, like real noise.
double gaussNoiseAt(int n) {
    double sum = 0;
    for (int k = 0; k < 4; ++k) sum += noiseSource(20 + k, n);
    return sum / 2.0 * 0.58;
}

enum class Var { T, X, N, Count };

struct Function {
    const char *name;
    int args;
};
const Function kFunctions[] = {
    {"sin", 1},   {"cos", 1},    {"tan", 1},   {"asin", 1},   {"acos", 1},  {"atan", 1},  {"sinh", 1},
    {"cosh", 1},  {"tanh", 1},   {"exp", 1},   {"ln", 1},     {"log", 1},   {"log10", 1}, {"sqrt", 1},
    {"abs", 1},   {"sign", 1},   {"floor", 1}, {"ceil", 1},   {"round", 1}, {"frac", 1},  {"sinc", 1},
    {"square", 1}, {"tri", 1},   {"saw", 1},   {"gauss", 1},  {"pulse", 2}, {"noise", 0}, {"pink", 0},
    {"brown", 0}, {"gnoise", 0}, {"min", 2},
    {"max", 2},   {"pow", 2},    {"mod", 2},   {"atan2", 2},  {"if", 3},
};
} // namespace

struct Expression::Node {
    enum Kind { Number, Variable, Negate, Binary, Call } kind = Number;
    double value = 0;
    Var var = Var::T;
    std::string op;   // binary operator
    int function = 0; // index in kFunctions
    std::vector<std::unique_ptr<Node>> args;

    double eval(double t, double x, int n, int count) const {
        switch (kind) {
        case Number: return value;
        case Variable:
            switch (var) {
            case Var::T: return t;
            case Var::X: return x;
            case Var::N: return n;
            case Var::Count: return count;
            }
            return 0;
        case Negate: return -args[0]->eval(t, x, n, count);
        case Binary: {
            const double a = args[0]->eval(t, x, n, count), b = args[1]->eval(t, x, n, count);
            if (op == "+") return a + b;
            if (op == "-") return a - b;
            if (op == "*") return a * b;
            if (op == "/") return a / b;
            if (op == "%") return std::fmod(a, b);
            if (op == "^") return std::pow(a, b);
            if (op == "<") return a < b;
            if (op == "<=") return a <= b;
            if (op == ">") return a > b;
            if (op == ">=") return a >= b;
            if (op == "==") return a == b;
            if (op == "!=") return a != b;
            return 0;
        }
        case Call: {
            const std::string name = kFunctions[function].name;
            if (name == "noise") return noiseAt(n);
            if (name == "pink") return pinkAt(n);
            if (name == "brown") return brownAt(n);
            if (name == "gnoise") return gaussNoiseAt(n);
            if (name == "if") return args[0]->eval(t, x, n, count) != 0 ? args[1]->eval(t, x, n, count)
                                                                          : args[2]->eval(t, x, n, count);
            const double a = args[0]->eval(t, x, n, count);
            if (args.size() == 2) {
                const double b = args[1]->eval(t, x, n, count);
                if (name == "pulse") return phaseOf(a) < b ? 1.0 : 0.0;
                if (name == "min") return std::min(a, b);
                if (name == "max") return std::max(a, b);
                if (name == "pow") return std::pow(a, b);
                if (name == "mod") return std::fmod(a, b);
                if (name == "atan2") return std::atan2(a, b);
                return 0;
            }
            if (name == "sin") return std::sin(a);
            if (name == "cos") return std::cos(a);
            if (name == "tan") return std::tan(a);
            if (name == "asin") return std::asin(a);
            if (name == "acos") return std::acos(a);
            if (name == "atan") return std::atan(a);
            if (name == "sinh") return std::sinh(a);
            if (name == "cosh") return std::cosh(a);
            if (name == "tanh") return std::tanh(a);
            if (name == "exp") return std::exp(a);
            if (name == "ln" || name == "log") return std::log(a);
            if (name == "log10") return std::log10(a);
            if (name == "sqrt") return std::sqrt(a);
            if (name == "abs") return std::fabs(a);
            if (name == "sign") return (a > 0) - (a < 0);
            if (name == "floor") return std::floor(a);
            if (name == "ceil") return std::ceil(a);
            if (name == "round") return std::round(a);
            if (name == "frac") return a - std::floor(a);
            if (name == "sinc") return std::fabs(a) < 1e-12 ? 1.0 : std::sin(a) / a;
            if (name == "square") return phaseOf(a) < 0.5 ? 1.0 : -1.0;
            if (name == "tri") return 2.0 / kPi * std::asin(std::sin(a)); // in phase with sin
            if (name == "saw") return 2.0 * phaseOf(a) - 1.0;
            if (name == "gauss") return std::exp(-a * a / 2.0);
            return 0;
        }
        }
        return 0;
    }
};

namespace {

class Parser {
  public:
    explicit Parser(const std::string &s) : text(s) {}

    std::unique_ptr<Expression::Node> run(std::string &error) {
        std::unique_ptr<Expression::Node> n = comparison();
        skip();
        if (!failed && pos < text.size()) fail("símbolo inesperado");
        if (failed) {
            error = message;
            return nullptr;
        }
        return n;
    }

  private:
    using NodePtr = std::unique_ptr<Expression::Node>;

    void skip() {
        while (pos < text.size() && std::isspace((unsigned char)text[pos])) ++pos;
    }
    bool accept(const char *s) {
        skip();
        const size_t len = std::strlen(s);
        if (text.compare(pos, len, s) == 0) {
            pos += len;
            return true;
        }
        return false;
    }
    void fail(const std::string &what) {
        if (failed) return;
        failed = true;
        message = what + " (posição " + std::to_string(pos + 1) + ")";
    }
    static NodePtr binary(const std::string &op, NodePtr a, NodePtr b) {
        NodePtr n(new Expression::Node);
        n->kind = Expression::Node::Binary;
        n->op = op;
        n->args.push_back(std::move(a));
        n->args.push_back(std::move(b));
        return n;
    }

    NodePtr comparison() {
        NodePtr a = additive();
        while (!failed) {
            const char *ops[] = {"<=", ">=", "==", "!=", "<", ">"};
            const char *found = nullptr;
            for (const char *o : ops)
                if (accept(o)) {
                    found = o;
                    break;
                }
            if (!found) break;
            a = binary(found, std::move(a), additive());
        }
        return a;
    }
    NodePtr additive() {
        NodePtr a = term();
        while (!failed) {
            if (accept("+"))
                a = binary("+", std::move(a), term());
            else if (accept("-"))
                a = binary("-", std::move(a), term());
            else
                break;
        }
        return a;
    }
    NodePtr term() {
        NodePtr a = unary();
        while (!failed) {
            if (accept("*"))
                a = binary("*", std::move(a), unary());
            else if (accept("/"))
                a = binary("/", std::move(a), unary());
            else if (accept("%"))
                a = binary("%", std::move(a), unary());
            else
                break;
        }
        return a;
    }
    NodePtr unary() {
        if (accept("-")) {
            NodePtr n(new Expression::Node);
            n->kind = Expression::Node::Negate;
            n->args.push_back(unary());
            return n;
        }
        if (accept("+")) return unary();
        return power();
    }
    NodePtr power() {
        NodePtr a = primary();
        if (!failed && accept("^")) return binary("^", std::move(a), unary()); // right associative
        return a;
    }
    NodePtr primary() {
        skip();
        NodePtr n(new Expression::Node);
        if (failed) return n;
        if (pos >= text.size()) {
            fail("fórmula incompleta");
            return n;
        }
        const char c = text[pos];
        if (std::isdigit((unsigned char)c) || c == '.') {
            const size_t used = readNumber(text.c_str() + pos, text.size() - pos, n->value);
            if (used == 0) {
                fail("número inválido");
                return n;
            }
            pos += used;
            if (pos < text.size() && text[pos] == ',' && pos + 1 < text.size() &&
                std::isdigit((unsigned char)text[pos + 1]) && depth == 0) {
                fail("use ponto como separador decimal (ex.: 0.5)");
            }
            return n;
        }
        if (accept("(")) {
            ++depth;
            NodePtr inner = comparison();
            --depth;
            if (!accept(")")) fail("falta fechar parênteses");
            return inner;
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t end = pos;
            while (end < text.size() && (std::isalnum((unsigned char)text[end]) || text[end] == '_')) ++end;
            const std::string name = text.substr(pos, end - pos);
            pos = end;
            skip();
            if (pos < text.size() && text[pos] == '(') {
                ++pos;
                int index = -1;
                for (int i = 0; i < (int)(sizeof kFunctions / sizeof kFunctions[0]); ++i)
                    if (name == kFunctions[i].name) index = i;
                if (index < 0) {
                    fail("função desconhecida: " + name);
                    return n;
                }
                n->kind = Expression::Node::Call;
                n->function = index;
                ++depth;
                if (!accept(")")) {
                    do {
                        n->args.push_back(comparison());
                    } while (!failed && accept(","));
                    if (!accept(")")) fail("falta fechar parênteses em " + name);
                }
                --depth;
                if (!failed && (int)n->args.size() != kFunctions[index].args)
                    fail(name + " precisa de " + std::to_string(kFunctions[index].args) + " argumento(s)");
                return n;
            }
            if (name == "pi") {
                n->value = kPi;
            } else if (name == "e") {
                n->value = std::exp(1.0);
            } else if (name == "t") {
                n->kind = Expression::Node::Variable;
                n->var = Var::T;
            } else if (name == "x") {
                n->kind = Expression::Node::Variable;
                n->var = Var::X;
            } else if (name == "n") {
                n->kind = Expression::Node::Variable;
                n->var = Var::N;
            } else if (name == "N") {
                n->kind = Expression::Node::Variable;
                n->var = Var::Count;
            } else {
                fail("nome desconhecido: " + name);
            }
            return n;
        }
        fail(std::string("símbolo inesperado '") + c + "'");
        return n;
    }

    const std::string &text;
    size_t pos = 0;
    int depth = 0;
    bool failed = false;
    std::string message;
};

std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && (std::isspace((unsigned char)s[a]) || s[a] == '"' || s[a] == '\'')) ++a;
    while (b > a && (std::isspace((unsigned char)s[b - 1]) || s[b - 1] == '"' || s[b - 1] == '\'')) --b;
    return s.substr(a, b - a);
}

bool toNumber(std::string s, bool decimalComma, double &v) {
    s = trim(s);
    if (s.empty()) return false;
    if (decimalComma) std::replace(s.begin(), s.end(), ',', '.');
    return readNumber(s.c_str(), s.size(), v) == s.size() && std::isfinite(v);
}

/// "12", "-0,125": a number written with a decimal comma (or an integer)
bool commaNumber(const std::string &t) {
    size_t i = (!t.empty() && (t[0] == '-' || t[0] == '+')) ? 1 : 0, digits = 0, commas = 0;
    for (; i < t.size(); ++i) {
        if (std::isdigit((unsigned char)t[i])) ++digits;
        else if (t[i] == ',' && ++commas == 1 && digits > 0) continue;
        else return false;
    }
    return digits > 0 && t.back() != ',';
}

std::vector<std::string> split(const std::string &line, char sep) {
    std::vector<std::string> out;
    if (sep == ' ') {
        size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && std::isspace((unsigned char)line[i])) ++i;
            size_t j = i;
            while (j < line.size() && !std::isspace((unsigned char)line[j])) ++j;
            if (j > i) out.push_back(line.substr(i, j - i));
            i = j;
        }
        return out;
    }
    size_t start = 0;
    while (true) {
        const size_t p = line.find(sep, start);
        out.push_back(line.substr(start, p == std::string::npos ? std::string::npos : p - start));
        if (p == std::string::npos) break;
        start = p + 1;
    }
    return out;
}

} // namespace

Expression::Expression() = default;
Expression::~Expression() = default;
Expression::Expression(Expression &&) noexcept = default;
Expression &Expression::operator=(Expression &&) noexcept = default;

bool Expression::parse(const std::string &text) {
    root.reset();
    message.clear();
    if (trim(text).empty()) {
        message = "fórmula vazia";
        return false;
    }
    Parser p(text);
    root = p.run(message);
    return root != nullptr;
}

double Expression::evaluate(int n, int count) const {
    if (!root || count <= 0) return 0;
    const double t = (double)n / count;
    return root->eval(t, kTwoPi * t, n, count);
}

bool sampleFormula(const std::string &formula, std::vector<double> &out, std::string &error, int count) {
    Expression e;
    if (!e.parse(formula)) {
        error = e.error();
        return false;
    }
    std::vector<double> v((size_t)count);
    for (int n = 0; n < count; ++n) {
        v[n] = e.evaluate(n, count);
        if (!std::isfinite(v[n])) {
            error = "a fórmula não dá um número em t = " + std::to_string((double)n / count) +
                    " (divisão por zero, raiz ou log de negativo?)";
            return false;
        }
    }
    out = std::move(v);
    return true;
}

std::vector<double> resample(const std::vector<double> &in, int count) {
    std::vector<double> out((size_t)std::max(count, 0));
    if (in.empty() || count <= 0) return out;
    if ((int)in.size() == count) return in;
    const size_t m = in.size();
    for (int j = 0; j < count; ++j) {
        const double pos = (double)j * m / count;
        const size_t i = (size_t)pos;
        const double f = pos - i;
        const double a = in[i % m], b = in[(i + 1) % m];
        // one period: the last segment goes back to the first point
        out[j] = a + (b - a) * f;
    }
    return out;
}

std::vector<int> toCodes(const std::vector<double> &values, Scaling scaling) {
    std::vector<int> out(values.size(), kMaxCode / 2 + 1);
    if (values.empty()) return out;
    double lo = -1, hi = 1;
    if (scaling == Scaling::Normalize) {
        lo = *std::min_element(values.begin(), values.end());
        hi = *std::max_element(values.begin(), values.end());
        if (!(hi > lo)) return out; // constant: middle of the range
    }
    for (size_t i = 0; i < values.size(); ++i) {
        const double u = (values[i] - lo) / (hi - lo);
        out[i] = (int)std::lround(std::min(1.0, std::max(0.0, u)) * kMaxCode);
    }
    return out;
}

std::vector<double> fromCodes(const std::vector<int> &codes) {
    std::vector<double> out(codes.size());
    for (size_t i = 0; i < codes.size(); ++i) out[i] = 2.0 * codes[i] / kMaxCode - 1.0;
    return out;
}

bool parseTable(const std::string &text, Table &table, std::string &error) {
    std::vector<std::string> lines;
    {
        size_t start = 0;
        while (start <= text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            std::string l = text.substr(start, end - start);
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (start == 0 && l.compare(0, 3, "\xEF\xBB\xBF") == 0) l.erase(0, 3); // UTF-8 BOM
            lines.push_back(l);
            if (end == text.size()) break;
            start = end + 1;
        }
    }
    // separator from the data lines (lines with letters are headers)
    char sep = ' ';
    bool any = false, dots = false, commas = false, onlyCommaNumbers = true;
    for (const std::string &l : lines) {
        const std::string t = trim(l);
        if (t.empty() || t[0] == '#') continue;
        any = true;
        if (t.find(';') != std::string::npos) {
            sep = ';';
            break;
        }
        if (t.find('\t') != std::string::npos) sep = '\t';
        dots = dots || t.find('.') != std::string::npos;
        commas = commas || t.find(',') != std::string::npos;
        const bool header = std::any_of(t.begin(), t.end(), [](char c) { return std::isalpha((unsigned char)c); });
        if (!header)
            for (const std::string &token : split(t, sep == '\t' ? '\t' : ' '))
                onlyCommaNumbers = onlyCommaNumbers && commaNumber(trim(token));
    }
    if (!any) {
        error = "o arquivo não tem números";
        return false;
    }
    // "0,125" one per line, or "1,5 2,5": the comma is the decimal separator, not a column separator
    if (sep == ' ' && commas && (dots || !onlyCommaNumbers)) sep = ',';
    const bool decimalComma = sep != ',';

    Table out;
    std::vector<std::vector<double>> rows;
    size_t width = 0;
    for (const std::string &l : lines) {
        const std::string t = trim(l);
        if (t.empty() || t[0] == '#') continue;
        const std::vector<std::string> fields = split(t, sep);
        std::vector<double> row;
        bool numeric = false;
        for (const std::string &f : fields) {
            double v = 0;
            if (toNumber(f, decimalComma, v)) {
                numeric = true;
                row.push_back(v);
            } else {
                row.push_back(std::nan(""));
            }
        }
        if (!numeric) {
            if (rows.empty() && out.header.empty()) // header line
                for (const std::string &f : fields) out.header.push_back(trim(f));
            continue;
        }
        width = std::max(width, row.size());
        rows.push_back(row);
    }
    if (rows.empty()) {
        error = "o arquivo não tem números";
        return false;
    }
    out.columns.assign(width, std::vector<double>(rows.size(), std::nan("")));
    for (size_t r = 0; r < rows.size(); ++r)
        for (size_t c = 0; c < rows[r].size(); ++c) out.columns[c][r] = rows[r][c];
    out.openHantekLog = out.header.size() >= 4 && out.header[0] == "aquisicao" && width >= 4;
    table = std::move(out);
    return true;
}

int openHantekAcquisitions(const Table &table) {
    if (!table.openHantekLog || table.columns.empty()) return 0;
    double last = 0;
    for (double v : table.columns[0])
        if (std::isfinite(v)) last = std::max(last, v);
    return (int)last;
}

std::vector<double> openHantekAcquisition(const Table &table, int acquisition) {
    std::vector<double> out;
    if (!table.openHantekLog || table.columns.size() < 4) return out;
    for (size_t r = 0; r < table.columns[0].size(); ++r)
        if (table.columns[0][r] == acquisition && std::isfinite(table.columns[3][r]))
            out.push_back(table.columns[3][r]);
    return out;
}

bool isDeviceFormat(const std::vector<double> &values) {
    if (values.size() != (size_t)kPoints) return false;
    for (double v : values)
        if (!(v >= 0 && v <= kMaxCode && v == std::floor(v))) return false;
    return true;
}

bool isSixteenBitFormat(const std::vector<double> &values) {
    if (values.size() != (size_t)kPoints) return false;
    bool above = false;
    for (double v : values) {
        if (!(v >= 0 && v <= 65535 && v == std::floor(v))) return false;
        above = above || v > kMaxCode;
    }
    return above;
}

std::string deviceFormatText(const std::vector<int> &codes) {
    std::string s;
    s.reserve(codes.size() * 6);
    for (int c : codes) s += std::to_string(std::min(kMaxCode, std::max(0, c))) + "\n";
    return s;
}

const std::vector<Example> &examples() {
    static const std::vector<Example> list = {
        {"Senoide", "sin(x)"},
        {"Senoide + 3ª harmônica", "sin(x) + 0.3*sin(3*x)"},
        {"Quadrada com 5 harmônicas", "sin(x) + sin(3*x)/3 + sin(5*x)/5 + sin(7*x)/7 + sin(9*x)/9"},
        {"Senoide amortecida", "exp(-5*t)*sin(20*x)"},
        {"Sinc", "sinc(20*pi*(t - 0.5))"},
        {"Pulso gaussiano", "gauss((t - 0.5)*20)"},
        {"AM 50 % (10 ciclos)", "(1 + 0.5*sin(x))*sin(10*x)"},
        {"Chirp (1 a 30 ciclos)", "sin(2*pi*(t + 14.5*t^2))"},
        {"Meia onda retificada", "max(sin(x), 0)"},
        {"Onda completa retificada", "abs(sin(x))"},
        {"Trem de pulsos 10 %", "pulse(5*x, 0.1)"},
        {"Escada de 8 degraus", "floor(8*t)/7"},
        {"Carga e descarga RC", "if(t < 0.5, 1 - exp(-10*t), exp(-10*(t - 0.5)))"},
        // sinusoidal PWM as in sine inverters: the sine compared with a triangle carrier (51 per period,
        // modulation index 0.9); with the channel at 60 Hz the carrier is 3060 Hz
        {"SPWM bipolar (inversor, 51 pulsos)", "if(0.9*sin(x) > tri(51*x), 1, -1)"},
        {"SPWM unipolar 3 níveis (51 pulsos)", "(0.9*sin(x) > tri(51*x)) - (-0.9*sin(x) > tri(51*x))"},
        {"SPWM bipolar (21 pulsos, fácil de ver)", "if(0.9*sin(x) > tri(21*x), 1, -1)"},
        {"Senoide com ruído", "sin(x) + 0.1*noise()"},
        // audio: with the channel at 5 Hz a period of 8192 points plays at 41 kS/s (up to ~20 kHz)
        {"Ruído branco (uniforme)", "noise()"},
        {"Ruído branco gaussiano (áudio)", "gnoise()"},
        {"Ruído rosa, -3 dB/oitava (áudio)", "pink()"},
        {"Ruído marrom, -6 dB/oitava (áudio)", "brown()"},
        {"ECG estilizado", "gauss((t-0.2)*40)*0.15 + gauss((t-0.35)*150) - 0.2*gauss((t-0.32)*150) - "
                           "0.25*gauss((t-0.38)*150) + 0.3*gauss((t-0.6)*25)"},
    };
    return list;
}

} // namespace arbwave
