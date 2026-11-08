#pragma once

namespace kodanuki::math {

struct Interval {
	double lower;
	double upper;
};

struct Dual {
	double value;
	double gradient[3];
};

struct HyperDual {
	double value;
	double gradient[3];
	double hessian[6];
};

struct IntervalHyperDual {
	Interval value;
	Interval gradient[3];
	Interval hessian[6];
};

constexpr int batchWidth = 8;

struct Batch {
	double at[batchWidth];
};

struct DualBatch {
	Dual at[batchWidth];
};

enum class Compare {
	Less,
	LessEqual,
	Equal,
	NotEqual,
	And,
	Or,
};

template <typename T>
T constantAs(double value);
template <typename T>
T coordinateAs(double value, int axis);
Interval operator+(const Interval& a, const Interval& b);
Interval operator-(const Interval& a, const Interval& b);
Interval operator*(const Interval& a, const Interval& b);
Interval operator/(const Interval& a, const Interval& b);
Interval operator-(const Interval& a);
Dual operator+(const Dual& a, const Dual& b);
Dual operator-(const Dual& a, const Dual& b);
Dual operator*(const Dual& a, const Dual& b);
Dual operator/(const Dual& a, const Dual& b);
Dual operator-(const Dual& a);
HyperDual operator+(const HyperDual& a, const HyperDual& b);
HyperDual operator-(const HyperDual& a, const HyperDual& b);
HyperDual operator*(const HyperDual& a, const HyperDual& b);
HyperDual operator/(const HyperDual& a, const HyperDual& b);
HyperDual operator-(const HyperDual& a);
IntervalHyperDual operator+(const IntervalHyperDual& a, const IntervalHyperDual& b);
IntervalHyperDual operator-(const IntervalHyperDual& a, const IntervalHyperDual& b);
IntervalHyperDual operator*(const IntervalHyperDual& a, const IntervalHyperDual& b);
IntervalHyperDual operator/(const IntervalHyperDual& a, const IntervalHyperDual& b);
IntervalHyperDual operator-(const IntervalHyperDual& a);
double sqrt(double a);
double abs(double a);
double floor(double a);
double sin(double a);
double cos(double a);
double sign(double a);
double atan2(double y, double x);
double pow(double a, double b);
double min(double a, double b);
double max(double a, double b);
Interval sqrt(const Interval& a);
Interval abs(const Interval& a);
Interval floor(const Interval& a);
Interval sin(const Interval& a);
Interval cos(const Interval& a);
Interval sign(const Interval& a);
Interval atan2(const Interval& y, const Interval& x);
Interval pow(const Interval& a, const Interval& b);
Interval min(const Interval& a, const Interval& b);
Interval max(const Interval& a, const Interval& b);
Dual sqrt(const Dual& a);
Dual abs(const Dual& a);
Dual floor(const Dual& a);
Dual sin(const Dual& a);
Dual cos(const Dual& a);
Dual sign(const Dual& a);
Dual atan2(const Dual& y, const Dual& x);
Dual pow(const Dual& a, const Dual& b);
Dual min(const Dual& a, const Dual& b);
Dual max(const Dual& a, const Dual& b);
HyperDual sqrt(const HyperDual& a);
HyperDual abs(const HyperDual& a);
HyperDual floor(const HyperDual& a);
HyperDual sin(const HyperDual& a);
HyperDual cos(const HyperDual& a);
HyperDual sign(const HyperDual& a);
HyperDual atan2(const HyperDual& y, const HyperDual& x);
HyperDual pow(const HyperDual& a, const HyperDual& b);
HyperDual min(const HyperDual& a, const HyperDual& b);
HyperDual max(const HyperDual& a, const HyperDual& b);
IntervalHyperDual sqrt(const IntervalHyperDual& a);
IntervalHyperDual abs(const IntervalHyperDual& a);
IntervalHyperDual floor(const IntervalHyperDual& a);
IntervalHyperDual sin(const IntervalHyperDual& a);
IntervalHyperDual cos(const IntervalHyperDual& a);
IntervalHyperDual sign(const IntervalHyperDual& a);
IntervalHyperDual atan2(const IntervalHyperDual& y, const IntervalHyperDual& x);
IntervalHyperDual pow(const IntervalHyperDual& a, const IntervalHyperDual& b);
IntervalHyperDual min(const IntervalHyperDual& a, const IntervalHyperDual& b);
IntervalHyperDual max(const IntervalHyperDual& a, const IntervalHyperDual& b);
double compare(Compare kind, double a, double b);
Interval compare(Compare kind, const Interval& a, const Interval& b);
Dual compare(Compare kind, const Dual& a, const Dual& b);
HyperDual compare(Compare kind, const HyperDual& a, const HyperDual& b);
IntervalHyperDual compare(Compare kind, const IntervalHyperDual& a, const IntervalHyperDual& b);
double choose(double condition, double a, double b);
Interval choose(const Interval& condition, const Interval& a, const Interval& b);
Dual choose(const Dual& condition, const Dual& a, const Dual& b);
HyperDual choose(const HyperDual& condition, const HyperDual& a, const HyperDual& b);
IntervalHyperDual choose(const IntervalHyperDual& condition, const IntervalHyperDual& a, const IntervalHyperDual& b);
double smoothMinimum(double a, double b, double r);
Interval smoothMinimum(const Interval& a, const Interval& b, const Interval& r);
Dual smoothMinimum(const Dual& a, const Dual& b, const Dual& r);
HyperDual smoothMinimum(const HyperDual& a, const HyperDual& b, const HyperDual& r);
IntervalHyperDual smoothMinimum(const IntervalHyperDual& a, const IntervalHyperDual& b, const IntervalHyperDual& r);
double laneValue(double a, int lane);
double laneValue(const Dual& a, int lane);
double laneValue(const DualBatch& a, int lane);
double positiveSquare(double a);
Interval positiveSquare(const Interval& a);
Dual positiveSquare(const Dual& a);
HyperDual positiveSquare(const HyperDual& a);
IntervalHyperDual positiveSquare(const IntervalHyperDual& a);
IntervalHyperDual coordinateOver(const Interval& span, int axis);
Batch operator+(const Batch& a, const Batch& b);
Batch operator-(const Batch& a, const Batch& b);
Batch operator*(const Batch& a, const Batch& b);
Batch operator/(const Batch& a, const Batch& b);
Batch operator-(const Batch& a);
Batch sqrt(const Batch& a);
Batch abs(const Batch& a);
Batch floor(const Batch& a);
Batch sin(const Batch& a);
Batch cos(const Batch& a);
Batch sign(const Batch& a);
Batch atan2(const Batch& y, const Batch& x);
Batch pow(const Batch& a, const Batch& b);
Batch min(const Batch& a, const Batch& b);
Batch max(const Batch& a, const Batch& b);
Batch compare(Compare kind, const Batch& a, const Batch& b);
Batch choose(const Batch& condition, const Batch& a, const Batch& b);
Batch smoothMinimum(const Batch& a, const Batch& b, const Batch& r);
Batch positiveSquare(const Batch& a);
DualBatch operator+(const DualBatch& a, const DualBatch& b);
DualBatch operator-(const DualBatch& a, const DualBatch& b);
DualBatch operator*(const DualBatch& a, const DualBatch& b);
DualBatch operator/(const DualBatch& a, const DualBatch& b);
DualBatch operator-(const DualBatch& a);
DualBatch sqrt(const DualBatch& a);
DualBatch abs(const DualBatch& a);
DualBatch floor(const DualBatch& a);
DualBatch sin(const DualBatch& a);
DualBatch cos(const DualBatch& a);
DualBatch sign(const DualBatch& a);
DualBatch atan2(const DualBatch& y, const DualBatch& x);
DualBatch pow(const DualBatch& a, const DualBatch& b);
DualBatch min(const DualBatch& a, const DualBatch& b);
DualBatch max(const DualBatch& a, const DualBatch& b);
DualBatch compare(Compare kind, const DualBatch& a, const DualBatch& b);
DualBatch choose(const DualBatch& condition, const DualBatch& a, const DualBatch& b);
DualBatch smoothMinimum(const DualBatch& a, const DualBatch& b, const DualBatch& r);
DualBatch positiveSquare(const DualBatch& a);

}
