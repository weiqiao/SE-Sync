/** This class encapsulates an instance of the rank-restricted Riemannian form
* of the semidefinite relaxation solved by SE-Sync (Problem 9 in the SE-Sync
* tech report).  It contains all of the precomputed and cached data matrices
* necessary to describe the problem and run the optimization algorithm, as well
* as functions for performing geometric operations on the underlying manifold
* (tangent space projection and retraction) and evaluating the optimization
* objective and its gradient and Hessian operator.
*
*  Copyright (C) 2016, 2017 by David M. Rosen
*/

#pragma once

/** Use external matrix factorizations/linear solves provided by SuiteSparse
 * (SPQR and Cholmod) */

#include <Eigen/CholmodSupport>
#include <Eigen/Dense>
#include <Eigen/SPQRSupport>
#include <Eigen/Sparse>

#include "SESync/RelativePoseMeasurement.h"
#include "SESync/SESync_types.h"
#include "SESync/SESync_utils.h"
#include "SESync/StiefelProduct.h"

namespace SESync {

/** The type of the sparse Cholesky factorization to use in the computation of
 * the orthogonal projection operation */
typedef Eigen::CholmodDecomposition<SparseMatrix> SparseCholeskyFactorization;

/** The type of the QR decomposition to use in the computation of the orthogonal
 * projection operation */
typedef Eigen::SPQR<SparseMatrix> SparseQRFactorization;

/** The type of the incomplete Cholesky decomposition we will use for
 * preconditioning the conjugate gradient iterations in the RTR method */
typedef Eigen::IncompleteCholesky<double> IncompleteCholeskyFactorization;

class SESyncProblem {
private:
  /// PROBLEM DATA

  /** The specific formulation of the SE-Sync problem to be solved
(translation-implicit, translation-explicit, or robust) */
  Formulation form;

  /** Number of poses */
  unsigned int n = 0;

  /** Number of measurements */
  unsigned int m = 0;

  /** Dimensional parameter d for the special Euclidean group SE(d) over which
   * this problem is defined */
  unsigned int d = 0;

  /** Relaxation rank */
  unsigned int r = 0;

  /** The oriented incidence matrix A encoding the underlying measurement
   * graph for this problem */
  SparseMatrix A;

  /** The matrices B1, B2, and B3 defined in equation (69) of the SE-Sync tech
   * report */
  SparseMatrix B1, B2, B3;

  /** The matrix M parameterizing the quadratic form appearing in the Explicit
   * form of the SE-Sync problem (Problem 2 in the SE-Sync tech report) */
  SparseMatrix M;

  /** The rotational connection Laplacian for the special Euclidean
* synchronization problem, cf. eq. 14 of the SE-Sync tech report.  Only
* used in Implicit mode.*/
  SparseMatrix LGrho;

  /** The weighted reduced oriented incidence matrix Ared Omega^(1/2) (cf.
* eq. 39 of the SE-Sync tech report).  Only used in Implicit mode. */
  SparseMatrix Ared_SqrtOmega;

  /** The transpose of the above matrix; we cache this for computational
 * efficiency, since it's used frequently.  Only used in Implicit mode.
 */
  SparseMatrix SqrtOmega_AredT;

  /** The weighted translational data matrix Omega^(1/2) T (cf. eqs. 22-24
* of the SE-Sync tech report.  Only used in Implicit mode. */
  SparseMatrix SqrtOmega_T;

  /** The transpose of the above matrix; we cache this for computational
 * efficiency, since it's used frequently.  Only used in Implicit mode. */
  SparseMatrix TT_SqrtOmega;

  /** An Eigen sparse linear solver that encodes the Cholesky factor L used
* in the computation of the orthogonal projection function (cf. eq. 39 of the
* SE-Sync tech report) */
  SparseCholeskyFactorization L;

  /** An Eigen sparse linear solver that encodes the QR factorization used in
 * the computation of the orthogonal projection function (cf. eq. 98 of the
 * SE-Sync tech report) */

  // When using Eigen::SPQR, the destructor causes a segfault if this variable
  // isn't explicitly initialized (i.e. not just default-constructed)
  SparseQRFactorization *QR = nullptr;

  /** A Boolean variable determining whether to use the Cholesky or QR
 * decompositions for computing the orthogonal projection */
  bool use_Cholesky;

  /** The preconditioning strategy to use when running the Riemannian
   * trust-region algorithm */
  Preconditioner preconditioner;

  /** Diagonal Jacobi preconditioner */
  DiagonalMatrix JacobiPrecon;

  /** Incomplete Cholesky Preconditioner */
  IncompleteCholeskyFactorization *iChol = nullptr;

  /** The underlying manifold in which the generalized orientations lie in the
  rank-restricted Riemannian optimization problem (Problem 9 in the SE-Sync tech
  report).*/
  StiefelProduct SP;

public:
  /// CONSTRUCTORS AND MUTATORS

  /** Default constructor; doesn't actually do anything */
  SESyncProblem() {}

  /** Constructor using a vector of relative pose measurements */
  SESyncProblem(
      const std::vector<SESync::RelativePoseMeasurement> &measurements,
      const Formulation &formulation = Simplified, bool Cholesky = true,
      const Preconditioner &precon = IncompleteCholesky);

  /** Set the maximum rank of the rank-restricted semidefinite relaxation */
  void set_relaxation_rank(unsigned int rank);

  /// ACCESSORS

  /** Returns the specific formulation of this SE-Sync problem */
  Formulation formulation() const { return form; }

  /** Returns the number of poses appearing in this problem */
  unsigned int num_poses() const { return n; }

  /** Returns the number of measurements in this problem */
  unsigned int num_measurements() const { return m; }

  /** Returns the dimensional parameter d for the special Euclidean group SE(d)
   * over which this problem is defined */
  unsigned int dimension() const { return d; }

  /** Returns the relaxation rank r of this problem */
  unsigned int relaxation_rank() const { return r; }

  /** Returns the oriented incidence matrix A of the underlying measurement
   * graph over which this problem is defined */
  const SparseMatrix &oriented_incidence_matrix() const { return A; }

  const StiefelProduct &manifold() const { return SP; }

  const DiagonalMatrix &JacobiPreconditioner() const { return JacobiPrecon; }

  const IncompleteCholeskyFactorization *
  IncompleteCholsekyPreconditioner() const {
    return iChol;
  }

  /// OPTIMIZATION AND GEOMETRY

  /** Given a matrix X, this function computes and returns the orthogonal
  *projection Pi * X */
  // We inline this function in order to take advantage of Eigen's ability
  // to optimize matrix expressions as compile time
  inline Matrix Pi_product(const Matrix &X) const {
    if (use_Cholesky)
      return X - SqrtOmega_AredT * L.solve(Ared_SqrtOmega * X);
    else {
      Eigen::MatrixXd PiX = X;
      for (unsigned int c = 0; c < X.cols(); c++) {
        // Eigen's SPQR support only supports solving with vectors(!) (i.e.
        // 1-column matrices)
        PiX.col(c) = X.col(c) - SqrtOmega_AredT * QR->solve(X.col(c));
      }
      return PiX;
    }
  }

  /** This function computes and returns the product QX */
  // We inline this function in order to take advantage of Eigen's ability to
  // optimize matrix expressions as compile time
  inline Matrix Q_product(const Matrix &X) const {
    return LGrho * X + TT_SqrtOmega * Pi_product(SqrtOmega_T * X);
  }

  /** Given a matrix Y, this function computes and returns the matrix product
  SY, where S is the matrix that determines the quadratic form defining the
  objective  F(Y) := tr(S * Y' * Y) for the SE-Sync problem.  More precisely:
  *
  * If formulation == Implicit, this returns Q * Y, where Q is as defined in
  equation (24) of the SE-Sync tech report.
  *
  * If formulation == Explicit, this returns M * Y, where M is as defined in
  equation (18) of the SE-Sync tech report. */
  Matrix data_matrix_product(const Matrix &Y) const;

  /** Given a matrix Y, this function computes and returns F(Y), the value of
   * the objective evaluated at X */
  double evaluate_objective(const Matrix &Y) const;

  /** Given a matrix Y, this function computes and returns nabla F(Y), the
   * *Euclidean* gradient of F at Y. */
  Matrix Euclidean_gradient(const Matrix &Y) const;

  /** Given a matrix Y in the domain D of the SE-Sync optimization problem and
   * the *Euclidean* gradient nabla F(Y) at Y, this function computes and
   * returns the *Riemannian* gradient grad F(Y) of F at Y */
  Matrix Riemannian_gradient(const Matrix &Y, const Matrix &nablaF_Y) const;

  /** Given a matrix Y in the domain D of the SE-Sync optimization problem, this
   * function computes and returns grad F(Y), the *Riemannian* gradient of F
   * at Y */
  Matrix Riemannian_gradient(const Matrix &Y) const;

  /** Given a matrix Y in the domain D of the SE-Sync optimization problem, the
   * *Euclidean* gradient nablaF_Y of F at Y, and a tangent vector dotY in
   * T_D(Y), the tangent space of the domain of the optimization problem at Y,
   * this function computes and returns Hess F(Y)[dotY], the action of the
   * Riemannian Hessian on dotY */
  Matrix Riemannian_Hessian_vector_product(const Matrix &Y,
                                           const Matrix &nablaF_Y,
                                           const Matrix &dotY) const;

  /** Given a matrix Y in the domain D of the SE-Sync optimization problem, and
   * a tangent vector dotY in T_D(Y), the tangent space of the domain of the
   * optimization problem at Y, this function computes and returns Hess
   * F(Y)[dotY], the action of the Riemannian Hessian on dotX */
  Matrix Riemannian_Hessian_vector_product(const Matrix &Y,
                                           const Matrix &dotY) const;

  /** Given a matrix Y in the domain D of the SE-Sync optimization problem, and
   * a tangent vector dotY in T_D(Y), this function applies the selected
   * preconditioning strategy to dotY */
  Matrix precondition(const Matrix &Y, const Matrix &dotY) const;

  /** Given a matrix Y in the domain D of the SE-Sync optimization problem and a
  tangent vector dotY in T_Y(E), the tangent space of Y considered as a generic
  matrix, this function computes and returns the orthogonal projection of dotY
  onto T_D(Y), the tangent space of the domain D at Y*/
  Matrix tangent_space_projection(const Matrix &Y, const Matrix &dotY) const;

  /** Given a matrix Y in the domain D of the SE-Sync optimization problem and a
   * tangent vector dotY in T_D(Y), this function returns the point Yplus in D
   * obtained by retracting along dotY */
  Matrix retract(const Matrix &Y, const Matrix &dotY) const;

  /** Given a point Y in the domain D of the rank-r relaxation of the SE-Sync
   * optimization problem, this function computes and returns a matrix X = [t |
   * R] comprised of translations and rotations for a set of feasible poses for
   * the original estimation problem obtained by rounding the point Y */
  Matrix round_solution(const Matrix Y) const;

  /** Given a critical point Y of the rank-r relaxation of the SE-Sync
   * optimization problem, this function computes and returns a d x nd matrix
   * comprised of dxd block elements of the associated block-diagonal Lagrange
   * multiplier matrix associated with the orthonormality constraints on the
   * generalized orientations of the poses (cf. eq. (119) in the SE-Sync tech
   * report) */
  Matrix compute_Lambda_blocks(const Matrix &Y) const;

  /** Given a critical point Y in the domain of the optimization problem, this
  *function computes the smallest eigenvalue lambda_min of S - Lambda and its
  *associated eigenvector v.  Returns a Boolean value indicating whether the
  *Lanczos method used to estimate the smallest eigenpair converged to
  *within the required tolerance. */
  bool compute_S_minus_Lambda_min_eig(
      const Matrix &Y, double &min_eigenvalue, Eigen::VectorXd &min_eigenvector,
      unsigned int max_iterations = 10000,
      double min_eigenvalue_nonnegativity_tolerance = 1e-5,
      unsigned int num_Lanczos_vectors = 20) const;

  /** Computes and returns the chordal initialization for the rank-restricted
   * semidefinite relaxation */
  Matrix chordal_initialization() const;

  /** Randomly samples a point in the domain for the rank-restricted
   * semidefinite relaxation */
  Matrix random_sample() const;

  ~SESyncProblem() {
    if (QR)
      delete QR;

    if (iChol)
      delete iChol;
  }

  /// MINIMUM EIGENVALUE COMPUTATIONS

  /** This is a lightweight struct used in conjunction with Spectra to compute
  *the minimum eigenvalue and eigenvector of S - Lambda(X); it has a single
  *nontrivial function, perform_op(x,y), that computes and returns the product
  *y = (S - Lambda + sigma*I) x */
  struct SMinusLambdaProdFunctor {
    const SESyncProblem *_problem;

    // Diagonal blocks of the matrix Lambda
    Matrix _Lambda_blocks;

    // Number of rows and columns of the matrix B - Lambda
    int _rows;
    int _cols;

    // Dimensional parameter d of the special Euclidean group SE(d) over which
    // this synchronization problem is defined
    int _dim;
    double _sigma;

    // Constructor
    SMinusLambdaProdFunctor(const SESyncProblem *prob, const Matrix &Y,
                            double sigma = 0);

    int rows() const { return _rows; }
    int cols() const { return _cols; }

    // Matrix-vector multiplication operation
    void perform_op(double *x, double *y) const;
  };
};
}
