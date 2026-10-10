// SPDX-License-Identifier: GPL-2.0+
// Unit tests of the arbitrary waveform helpers (formulas, files, 14-bit codes).

#include "generator/arbwave.h"
#include "testing.h"

#include <cmath>

using namespace arbwave;

static double at(const std::string &formula, int n, int count = 8) {
    Expression e;
    if (!e.parse(formula)) return std::nan("");
    return e.evaluate(n, count);
}

static void formulas() {
    CHECK_NEAR(at("1 + 2*3", 0), 7, 1e-12);
    CHECK_NEAR(at("2^3^2", 0), 512, 1e-9); // right associative
    CHECK_NEAR(at("-2^2", 0), -4, 1e-12);
    CHECK_NEAR(at("(1+2)*3", 0), 9, 1e-12);
    CHECK_NEAR(at("t", 4), 0.5, 1e-12);
    CHECK_NEAR(at("x", 2), 3.14159265358979 / 2, 1e-12);
    CHECK_NEAR(at("n + N", 3), 11, 1e-12);
    CHECK_NEAR(at("sin(x)", 2), 1, 1e-12);
    CHECK_NEAR(at("square(x)", 1), 1, 0);
    CHECK_NEAR(at("square(x)", 5), -1, 0);
    CHECK_NEAR(at("tri(x)", 2), 1, 1e-12);
    CHECK_NEAR(at("saw(x)", 4), 0, 1e-12);
    CHECK_NEAR(at("pulse(x, 0.25)", 1), 1, 0);
    CHECK_NEAR(at("pulse(x, 0.25)", 3), 0, 0);
    CHECK_NEAR(at("sinc(0)", 0), 1, 0);
    CHECK_NEAR(at("if(t < 0.5, 1, -1)", 6), -1, 0);
    CHECK_NEAR(at("max(2, 3) + min(2, 3)", 0), 5, 0);
    CHECK_NEAR(at("t >= 0.5", 4), 1, 0);
    CHECK(at("noise()", 3) == at("noise()", 3)); // deterministic
    CHECK(std::fabs(at("noise()", 3)) <= 1);

    Expression e;
    CHECK(!e.parse("sin(x"));
    CHECK(!e.parse("foo(1)"));
    CHECK(!e.parse("y + 1"));
    CHECK(!e.parse("sin(1, 2)"));
    CHECK(!e.parse("0,5*sin(x)")); // decimal comma: the message asks for a point
    CHECK(e.error().find("ponto") != std::string::npos);
    CHECK(!e.parse(""));

    std::vector<double> v;
    std::string err;
    CHECK(sampleFormula("sin(x)", v, err));
    CHECK_EQ(v.size(), (size_t)kPoints);
    CHECK(!sampleFormula("1/(t-t)", v, err)); // not finite
    for (const Example &ex : examples()) CHECK(sampleFormula(ex.formula, v, err));
}

static void codes() {
    const std::vector<int> c = toCodes({-2, 0, 2}, Scaling::Normalize);
    CHECK_EQ(c[0], 0);
    CHECK_EQ(c[1], 8192);
    CHECK_EQ(c[2], 16383);
    const std::vector<int> f = toCodes({-1, 0, 1, 3}, Scaling::Fixed);
    CHECK_EQ(f[0], 0);
    CHECK_EQ(f[2], 16383);
    CHECK_EQ(f[3], 16383); // clipped
    CHECK_EQ(toCodes({5, 5}, Scaling::Normalize)[0], 8192);
    const std::vector<double> back = fromCodes({0, 16383});
    CHECK_NEAR(back[0], -1, 1e-12);
    CHECK_NEAR(back[1], 1, 1e-12);

    const std::vector<double> r = resample({0, 1, 2, 3}, 8);
    CHECK_EQ(r.size(), (size_t)8);
    CHECK_NEAR(r[1], 0.5, 1e-12);
    CHECK_NEAR(r[6], 3, 1e-12);
    CHECK_NEAR(r[7], 1.5, 1e-12); // last segment wraps to the first point
    CHECK_EQ(resample(std::vector<double>(100, 1.0)).size(), (size_t)kPoints);
}

static void files() {
    Table t;
    std::string err;
    CHECK(parseTable("1\n2\n3\n", t, err));
    CHECK_EQ(t.columns.size(), (size_t)1);
    CHECK_NEAR(t.columns[0][2], 3, 0);

    // OpenHantek waveform log: ';' and decimal comma, date column, two acquisitions
    const std::string log = "\xEF\xBB\xBF# OpenHantek DSO-2250 - registro do CH1\n"
                            "# inicio:;2026-10-09 18:00:00;ponteira/garra:;x10\n"
                            "aquisicao;data_hora;t (s);valor (V)\n"
                            "1;2026-10-09 18:00:00;0;0,5\n1;2026-10-09 18:00:00;0,001;-0,5\n"
                            "2;2026-10-09 18:00:01;0;1,5\n";
    CHECK(parseTable(log, t, err));
    CHECK(t.openHantekLog);
    CHECK_EQ(openHantekAcquisitions(t), 2);
    const std::vector<double> a1 = openHantekAcquisition(t, 1);
    CHECK_EQ(a1.size(), (size_t)2);
    CHECK_NEAR(a1[1], -0.5, 1e-12);
    CHECK_NEAR(openHantekAcquisition(t, 2)[0], 1.5, 1e-12);

    // CSV with a header and ',' separator
    CHECK(parseTable("time,ch1\n0.0,1.5\n0.1,2.5\n", t, err));
    CHECK_EQ(t.header.size(), (size_t)2);
    CHECK_NEAR(t.columns[1][1], 2.5, 1e-12);
    CHECK(!parseTable("# nada\n\n", t, err));

    // one column with decimal commas, and space separated decimal commas
    CHECK(parseTable("0,125\n0,5\n-0,25\n", t, err));
    CHECK_EQ(t.columns.size(), (size_t)1);
    CHECK_NEAR(t.columns[0][2], -0.25, 1e-12);
    CHECK(parseTable("1,5 2,5\n3 4,25\n", t, err));
    CHECK_EQ(t.columns.size(), (size_t)2);
    CHECK_NEAR(t.columns[1][1], 4.25, 1e-12);
    // integers separated by commas stay columns
    CHECK(parseTable("1,2,3\n4,5,6\n", t, err));
    CHECK_EQ(t.columns.size(), (size_t)3);

    std::vector<double> device(kPoints, 100);
    CHECK(isDeviceFormat(device));
    CHECK(!isSixteenBitFormat(device));
    device[5] = 40000;
    CHECK(!isDeviceFormat(device));
    CHECK(isSixteenBitFormat(device));
    CHECK_EQ(deviceFormatText({1, 2, 99999}), std::string("1\n2\n16383\n"));
}

#include <clocale>

int main() {
    // the GUI runs with the system locale (pt_BR: decimal comma); parsing must not depend on it
    if (!std::setlocale(LC_NUMERIC, "pt_BR.UTF-8")) std::setlocale(LC_NUMERIC, "de_DE.UTF-8");
    formulas();
    codes();
    files();
    return testing::report("arbwave");
}
