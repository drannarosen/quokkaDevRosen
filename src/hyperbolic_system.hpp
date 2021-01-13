#ifndef HYPERBOLIC_SYSTEM_HPP_ // NOLINT
#define HYPERBOLIC_SYSTEM_HPP_
//==============================================================================
// TwoMomentRad - a radiation transport library for patch-based AMR codes
// Copyright 2020 Benjamin Wibking.
// Released under the MIT license. See LICENSE file included in the GitHub repo.
//==============================================================================
/// \file hyperbolic_system.hpp
/// \brief Defines classes and functions for use with hyperbolic systems of
/// conservation laws.
///
/// This file provides classes, data structures and functions for hyperbolic
/// systems of conservation laws.
///

// c++ headers
#include <cassert>
#include <cmath>

// library headers
#include "AMReX_Array4.H"
#include "AMReX_FArrayBox.H"

// internal headers


/// Provide type-safe global sign ('sgn') function.
template <typename T> auto sgn(T val) -> int
{
	return (T(0) < val) - (val < T(0));
}

using amrex::Real;

struct array_t
{
	amrex::Array4<Real> arr_;
	int ncomp_accessor_;

	array_t() : arr_(), ncomp_accessor_(0)
	{
		// default constructor
	}

	array_t(amrex::Array4<Real> arr) : arr_(arr), ncomp_accessor_(0)
	{
		// initialize arr_ to arr
	}

	array_t(amrex::Array4<Real> arr, int ncomp) : arr_(arr), ncomp_accessor_(ncomp)
	{

	}

	AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE 
	double& operator()(int n, int i) noexcept
	{
		int j = 0;
		int k = 0;
		return arr_(i, j, k, n);
	}

	AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE 
	double operator()(int n, int i) const noexcept
	{
		int j = 0;
		int k = 0;
		return arr_(i, j, k, n);
	}

	AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE 
	double& operator()(int i) noexcept
	{
		return arr_(i, 0, 0, ncomp_accessor_);
	}

	AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE 
	double operator()(int i) const noexcept
	{
		return arr_(i, 0, 0, ncomp_accessor_);
	}

	void AllocateArray(int ncomp, int dim1, int dim2 = 1, int dim3 = 1);
	auto SliceArray(int ncomp) -> array_t;
};

/// Class for a hyperbolic system of conservation laws (Cannot be instantiated,
/// must be subclassed.)
template <typename problem_t> class HyperbolicSystem
{
      public:
	array_t consVar_;

	/// Computes timestep and advances system
	void AdvanceTimestep();
	virtual void AdvanceTimestep(double dt_max);
	void AdvanceTimestepRK2(double dt_max);

	// setter functions:

	void set_cflNumber(double cflNumber);

	// accessor functions:

	[[nodiscard]] auto nvars() const -> int;
	[[nodiscard]] auto nghost() const -> int;
	[[nodiscard]] auto nx() const -> int;
	[[nodiscard]] auto dim1() const -> int;
	[[nodiscard]] auto time() const -> double;
	[[nodiscard]] auto dt() const -> double;

	// inline functions:

	__attribute__((always_inline)) inline static auto minmod(double a,
								 double b)
	    -> double
	{
		return 0.5 * (sgn(a) + sgn(b)) *
		       std::min(std::abs(a), std::abs(b));
	}

	__attribute__((always_inline)) inline static auto MC(double a, double b)
	    -> double
	{
		return 0.5 * (sgn(a) + sgn(b)) *
		       std::min(0.5 * std::abs(a + b),
				std::min(2.0 * std::abs(a), 2.0 * std::abs(b)));
	}

	virtual void FillGhostZones(array_t &cons);
	virtual void ConservedToPrimitive(array_t &cons,
					  std::pair<int, int> range) = 0;
	virtual void AddSourceTerms(array_t &U, std::pair<int, int> range) = 0;
	virtual auto CheckStatesValid(array_t &cons,
				      std::pair<int, int> range) -> bool;
	virtual void FlattenShocks(array_t &q, std::pair<int, int> range);

      protected:
	array_t primVar_;
	array_t consVarPredictStep_;
	array_t consVarPredictStepPrev_;
	array_t x1LeftState_;
	array_t x1RightState_;
	array_t x1Flux_;
	array_t x1FluxDiffusive_;

	double cflNumber_ = 1.0;
	double dt_ = 0;
	const double dtExpandFactor_ = 1.2;
	double dtPrev_ = std::numeric_limits<double>::max();
	double time_ = 0.;
	double lx_;
	double dx_;
	int nx_;
	int dim1_;
	int nvars_;
	const int nghost_ = 4; // 4 ghost cells required for PPM

	HyperbolicSystem(int nx, double lx, double cflNumber, int nvars)
	    : nx_(nx), lx_(lx), dx_(lx / static_cast<double>(nx)),
	      cflNumber_(cflNumber), nvars_(nvars)
	{
		assert(lx_ > 0.0);				   // NOLINT
		assert(nx_ > 2);				   // NOLINT
		assert(nghost_ > 1);				   // NOLINT
		assert((cflNumber_ > 0.0) && (cflNumber_ <= 1.0)); // NOLINT

		dim1_ = nx_ + 2 * nghost_;

		consVar_.AllocateArray(nvars_, dim1_);
		primVar_.AllocateArray(nvars_, dim1_);
		consVarPredictStep_.AllocateArray(nvars_, dim1_);
		consVarPredictStepPrev_.AllocateArray(nvars_, dim1_);

		x1LeftState_.AllocateArray(nvars_, dim1_);
		x1RightState_.AllocateArray(nvars_, dim1_);
		x1Flux_.AllocateArray(nvars_, dim1_);
		x1FluxDiffusive_.AllocateArray(nvars_, dim1_);
	}

	virtual void AddFluxesRK2(array_t &U0, array_t &U1);

	void ReconstructStatesConstant(array_t &q, std::pair<int, int> range);
	void ReconstructStatesPPM(array_t &q, std::pair<int, int> range);

	template <typename F>
	void ReconstructStatesPLM(array_t &q, std::pair<int, int> range,
				  F &&limiter);

	virtual void PredictStep(std::pair<int, int> range);
	auto ComputeTimestep() -> double;

	virtual auto ComputeTimestep(double dt_max) -> double = 0;
	virtual void ComputeFluxes(std::pair<int, int> range) = 0;
};

template <typename problem_t>
auto HyperbolicSystem<problem_t>::time() const -> double
{
	return time_;
}

template <typename problem_t>
auto HyperbolicSystem<problem_t>::dt() const -> double
{
	return dt_;
}

template <typename problem_t>
auto HyperbolicSystem<problem_t>::nx() const -> int
{
	return nx_;
}

template <typename problem_t>
auto HyperbolicSystem<problem_t>::dim1() const -> int
{
	return dim1_;
}

template <typename problem_t>
auto HyperbolicSystem<problem_t>::nghost() const -> int
{
	return nghost_;
}

template <typename problem_t>
auto HyperbolicSystem<problem_t>::nvars() const -> int
{
	return nvars_;
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::set_cflNumber(double cflNumber)
{
	assert((cflNumber > 0.0) && (cflNumber <= 1.0)); // NOLINT
	cflNumber_ = cflNumber;
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::AddSourceTerms(array_t &U,
						 std::pair<int, int> range)
{
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::FlattenShocks(array_t &q, std::pair<int, int> range)
{
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::FillGhostZones(array_t &cons)
{
	// In general, this step will require MPI communication, and interaction
	// with the main AMR code.

	// periodic boundary conditions
#if 0
	// x1 right side boundary
	for (int n = 0; n < nvars_; ++n) {
		for (int i = nghost_ + nx_; i < nghost_ + nx_ + nghost_; ++i) {
			cons(n, i) = cons(n, i - nx_);
		}
	}

	// x1 left side boundary
	for (int n = 0; n < nvars_; ++n) {
		for (int i = 0; i < nghost_; ++i) {
			cons(n, i) = cons(n, i + nx_);
		}
	}
#endif

	// extrapolate boundary conditions
	// x1 right side boundary
	for (int n = 0; n < nvars_; ++n) {
		for (int i = nghost_ + nx_; i < nghost_ + nx_ + nghost_; ++i) {
			cons(n, i) = cons(n, nghost_ + nx_ - 1);
		}
	}

	// x1 left side boundary
	for (int n = 0; n < nvars_; ++n) {
		for (int i = 0; i < nghost_; ++i) {
			cons(n, i) = cons(n, nghost_ + 0);
		}
	}
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::ReconstructStatesConstant(
    array_t &q, const std::pair<int, int> range)
{
	// By convention, the interfaces are defined on the left edge of each
	// zone, i.e. xleft_(i) is the "left"-side of the interface at
	// the left edge of zone i, and xright_(i) is the "right"-side of the
	// interface at the *left* edge of zone i.

	// Indexing note: There are (nx + 1) interfaces for nx zones.
	for (int n = 0; n < nvars_; ++n) {
		for (int i = range.first; i < (range.second + 1); ++i) {

			// Use piecewise-constant reconstruction
			// (This converges at first order in spatial
			// resolution.)

			x1LeftState_(n, i) = q(n, i - 1);
			x1RightState_(n, i) = q(n, i);
		}
	}
}

template <typename problem_t>
template <typename F>
void HyperbolicSystem<problem_t>::ReconstructStatesPLM(
    array_t &q, const std::pair<int, int> range, F &&limiter)
{
	// By convention, the interfaces are defined on the left edge of each
	// zone, i.e. xleft_(i) is the "left"-side of the interface at
	// the left edge of zone i, and xright_(i) is the "right"-side of the
	// interface at the *left* edge of zone i.

	// Indexing note: There are (nx + 1) interfaces for nx zones.

	for (int n = 0; n < nvars_; ++n) {
		for (int i = range.first; i < (range.second + 1); ++i) {

			// Use piecewise-linear reconstruction
			// (This converges at second order in spatial
			// resolution.)

			const auto lslope = limiter(q(n, i) - q(n, i - 1),
						    q(n, i - 1) - q(n, i - 2));

			const auto rslope = limiter(q(n, i + 1) - q(n, i),
						    q(n, i) - q(n, i - 1));

			x1LeftState_(n, i) =
			    q(n, i - 1) + 0.25 * lslope;	       // NOLINT
			x1RightState_(n, i) = q(n, i) - 0.25 * rslope; // NOLINT
		}
	}
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::ReconstructStatesPPM(
    array_t &q, const std::pair<int, int> range)
{
	// By convention, the interfaces are defined on the left edge of each
	// zone, i.e. xleft_(i) is the "left"-side of the interface at the left
	// edge of zone i, and xright_(i) is the "right"-side of the interface
	// at the *left* edge of zone i.

	// Indexing note: There are (nx + 1) interfaces for nx zones.

	for (int n = 0; n < nvars_; ++n) {
		for (int i = range.first; i < (range.second + 1); ++i) {
			// PPM reconstruction following Colella & Woodward
			// (1984), with some modifications following Mignone
			// (2014), as implemented in Athena++.

			// (1.) Estimate the interface a_{i - 1/2}.
			//      Equivalent to step 1 in Athena++
			//      [ppm_simple.cpp].

			// C&W Eq. (1.9) [parabola midpoint for the case of
			// equally-spaced zones]: a_{j+1/2} = (7/12)(a_j +
			// a_{j+1}) - (1/12)(a_{j+2} + a_{j-1}). Terms are
			// grouped to preserve exact symmetry in floating-point
			// arithmetic, following Athena++.

			const double coef_1 = (7. / 12.);
			const double coef_2 = (-1. / 12.);
			const double interface =
				(coef_1 * q(n, i) + coef_2 * q(n, i + 1)) +
				(coef_1 * q(n, i - 1) + coef_2 * q(n, i - 2));
#if 0
			// something is wrong here, this causes weird oscillations in Shu-Osher problem
			// -- do not use in production! --
			// Compute limited slopes as in C&W original method
			const double dq0 =
			    MC(q(n, i + 1) - q(n, i), q(n, i) - q(n, i - 1));

			const double dq1 =
				MC(q(n, i) - q(n, i - 1), q(n, i - 1) - q(n, i - 2));

			// Compute interface (i - 1/2)
			const double interface = q(n, i - 1) +
						 0.5 * (q(n, i) - q(n, i - 1)) -
						 (1. / 6.) * (dq0 - dq1);
#endif
			// a_R,(i-1) in C&W
			x1LeftState_(n, i) = interface;

			// a_L,i in C&W
			x1RightState_(n, i) = interface;
		}
	}

	for (int n = 0; n < nvars_; ++n) {
		for (int i = range.first; i < range.second; ++i) {
			// (2.) Constrain interface value to lie between
			// adjacent cell-averaged values (equivalent to
			// step 2b in Athena++ [ppm_simple.cpp]).
			// [See Eq. B8 of Mignone+ 2005]

			// compute bounds from surrounding cells
			const std::pair<double, double> bounds = 
				std::minmax({q(n, i - 1), q(n, i), q(n, i+1)}); // modify in 3d !!

			// get interfaces
			const double a_minus = x1RightState_(n, i);
			const double a_plus = x1LeftState_(n, i+1);

			// left side of zone i
			const double new_a_minus =
			    std::clamp(a_minus, bounds.first, bounds.second);

			// right side of zone i
			const double new_a_plus =
				std::clamp(a_plus, bounds.first, bounds.second);

			x1RightState_(n, i) = new_a_minus;
			x1LeftState_(n, i+1) = new_a_plus;
		}
	}

	for (int n = 0; n < nvars_; ++n) {
		for (int i = range.first; i < range.second; ++i) {

			const double a_minus =
			    x1RightState_(n, i); // a_L,i in C&W
			const double a_plus =
			    x1LeftState_(n, i + 1); // a_R,i in C&W
			const double a = q(n, i);   // a_i in C&W

			const double dq_minus = (a - a_minus);
			const double dq_plus = (a_plus - a);

			double new_a_minus = a_minus;
			double new_a_plus = a_plus;

			// (3.) Monotonicity correction, using Eq. (1.10) in PPM
			// paper. Equivalent to step 4b in Athena++
			// [ppm_simple.cpp].

			const double qa = dq_plus * dq_minus; // interface extrema

			if ((qa <= 0.0)) { // local extremum

				// Causes subtle, but very weird, oscillations
				// in the Shu-Osher test problem.
				// However, it is necessary to get a reasonable solution
				// for the sawtooth advection problem.
				const double dq0 = MC(q(n, i + 1) - q(n, i),
						      q(n, i) - q(n, i - 1));

				// use linear reconstruction, following Balsara (2017).
				//     [Living Rev Comput Astrophys (2017) 3:2]
				new_a_minus = a - 0.5 * dq0;
				new_a_plus = a + 0.5 * dq0;

				// original C&W method for this case
				//new_a_minus = a;
				//new_a_plus = a;

			} else { // no local extrema

				// parabola overshoots near
				// a_plus -> reset a_minus
				if (std::abs(dq_minus) >=
				    2.0 * std::abs(dq_plus)) {
					new_a_minus = a - 2.0 * dq_plus;
				}

				// parabola overshoots near
				// a_minus -> reset a_plus
				if (std::abs(dq_plus) >=
				    2.0 * std::abs(dq_minus)) {
					new_a_plus = a + 2.0 * dq_minus;
				}

			}

			x1RightState_(n, i) = new_a_minus;
			x1LeftState_(n, i + 1) = new_a_plus;
		}
	}

	// Important final step: ensure that velocity does not exceed c
	// in any cell where v^2 > c, reconstruct using first-order method for all velocity components
	// (must be done by user)

	// Apply shock flattening
	FlattenShocks(q, range);
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::PredictStep(const std::pair<int, int> range)
{
	// By convention, the fluxes are defined on the left edge of each zone,
	// i.e. flux_(i) is the flux *into* zone i through the interface on the
	// left of zone i, and -1.0*flux(i+1) is the flux *into* zone i through
	// the interface on the right of zone i.

	for (int n = 0; n < nvars_; ++n) {
		for (int i = range.first; i < range.second; ++i) {
			consVarPredictStep_(n, i) =
			    consVar_(n, i) -
			    (dt_ / dx_) * (x1Flux_(n, i + 1) - x1Flux_(n, i));
		}
	}
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::AddFluxesRK2(array_t &U0, array_t &U1)
{
	// By convention, the fluxes are defined on the left edge of each zone,
	// i.e. flux_(i) is the flux *into* zone i through the interface on the
	// left of zone i, and -1.0*flux(i+1) is the flux *into* zone i through
	// the interface on the right of zone i.

	for (int n = 0; n < nvars_; ++n) {
		for (int i = nghost_; i < nx_ + nghost_; ++i) {
			// RK-SSP2 integrator
			const double U_0 = U0(n, i);
			const double U_1 = U1(n, i);
			const double FU_1 = -1.0 * (dt_ / dx_) *
					    (x1Flux_(n, i + 1) - x1Flux_(n, i));

			// save results in U0
			U0(n, i) = 0.5 * U_0 + 0.5 * U_1 + 0.5 * FU_1;
		}
	}
}

template <typename problem_t>
auto HyperbolicSystem<problem_t>::ComputeTimestep() -> double
{
	return ComputeTimestep(std::numeric_limits<double>::max());
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::AdvanceTimestep()
{
	AdvanceTimestep(std::numeric_limits<double>::max());
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::AdvanceTimestep(const double dt_max)
{
	// use RK2 by default
	AdvanceTimestepRK2(dt_max);
}

template <typename problem_t>
auto HyperbolicSystem<problem_t>::CheckStatesValid(
    array_t &cons, const std::pair<int, int> range) -> bool
{
	return true;
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::AdvanceTimestepRK2(const double dt)
{
	const auto ppm_range = std::make_pair(-1 + nghost_, nx_ + 1 + nghost_);
	const auto cell_range = std::make_pair(nghost_, nx_ + nghost_);

	// Initialize data
	FillGhostZones(consVar_);
	ConservedToPrimitive(consVar_, std::make_pair(0, dim1_));
	//ComputeTimestep(std::min(dt_max, dtExpandFactor_ * dtPrev_));
	dt_ = dt;

	// Predictor step t_{n+1}
	FillGhostZones(consVar_);
	ConservedToPrimitive(consVar_, std::make_pair(0, dim1_));
	ReconstructStatesPPM(primVar_, ppm_range);
	ComputeFluxes(cell_range);
	PredictStep(cell_range);

	if (!CheckStatesValid(consVarPredictStep_, cell_range)) {
		// need to implement first-order flux correction here
		assert(false);
	}

	// Corrector step
	FillGhostZones(consVarPredictStep_);
	ConservedToPrimitive(consVarPredictStep_, std::make_pair(0, dim1_));
	ReconstructStatesPPM(primVar_, ppm_range);
	ComputeFluxes(cell_range);
	AddFluxesRK2(consVar_, consVarPredictStep_);

	if (!CheckStatesValid(consVarPredictStep_, cell_range)) {
		std::cout << "[stage 2] This should not happen!\n";
		assert(false); // NOLINT
	}

	// Add source terms via operator splitting
	AddSourceTerms(consVar_, cell_range);

#if 0
	if (CheckStatesValid(consVar_, cell_range)) {
		std::cout << "[source terms] This should not happen!\n";
		assert(false);
	}
#endif

	// Adjust our clock
	time_ += dt_;
	dtPrev_ = dt_;
}

#endif // HYPERBOLIC_SYSTEM_HPP_
