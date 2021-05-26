#pragma once

#include <deal.II/base/exceptions.h>
#include <deal.II/base/mpi.h>
#include <deal.II/base/utilities.h>

#include <deal.II/fe/fe.h>
#include <deal.II/fe/mapping_q_generic.h>

#include <deal.II/matrix_free/matrix_free.h>

#include <adapter/arbitrary_interface.h>
#include <adapter/dealii_interface.h>
#include <precice/SolverInterface.hpp>
#include <q_equidistant.h>

#include <ostream>

namespace Adapter
{
  using namespace dealii;

  /**
   * The Adapter class keeps together with the CouplingInterfaes all
   * functionalities to couple deal.II to other solvers with preCICE i.e. data
   * structures are set up, necessary information is passed to preCICE etc.
   */
  template <int dim,
            int fe_degree,
            typename VectorType,
            typename VectorizedArrayType = VectorizedArray<double>>
  class Adapter
  {
  public:
    /**
     * @brief      Constructor, which sets up the precice Solverinterface
     *
     * @param[in]  parameters Parameter class, which hold the data specified
     *             in the parameters.prm file
     * @param[in]  dealii_boundary_interface_id Boundary ID of the
     *             triangulation, which is associated with the coupling
     *             interface.
     * @param[in]  data The applied matrix-free object
     * @param[in]  dof_index Index of the relevant dof_handler in the
     *             corresponding MatrixFree object
     * @param[in]  read_quad_index Index of the quadrature formula in the
     *             corresponding MatrixFree object which should be used for data
     *             reading
     * @param[in]  write_quad_index Index of the quadrature formula in the
     *             corresponding MatrixFree object which should be used for data
     *             writing
     */
    template <typename ParameterClass>
    Adapter(const ParameterClass &parameters,
            const unsigned int    dealii_boundary_interface_id,
            std::shared_ptr<MatrixFree<dim, double, VectorizedArrayType>> data,
            const unsigned int dof_index        = 0,
            const unsigned int read_quad_index  = 0,
            const unsigned int write_quad_index = 1);

    /**
     * @brief      Initializes preCICE and passes all relevant data to preCICE
     *
     * @param[in]  dealii_to_precice Data, which should be given to preCICE and
     *             exchanged with other participants. Wether this data is
     *             required already in the beginning depends on your
     *             individual configuration and preCICE determines it
     *             automatically. In many cases, this data will just represent
     *             your initial condition.
     */
    void
    initialize(const VectorType &dealii_to_precice);

    /**
     * @brief      Advances preCICE after every timestep, converts data formats
     *             between preCICE and dealii
     *
     * @param[in]  dealii_to_precice Same data as in @p initialize_precice() i.e.
     *             data, which should be given to preCICE after each time step
     *             and exchanged with other participants.
     * @param[in]  computed_timestep_length Length of the timestep used by
     *             the solver.
     */
    void
    advance(const VectorType &dealii_to_precice,
            const double      computed_timestep_length);

    /**
     * @brief      Saves current state of time dependent variables in case of an
     *             implicit coupling
     *
     * @param[in]  state_variables Vector containing all variables to store as
     *             reference
     *
     * @note       This function only makes sense, if it is used with
     *             @p reload_old_state_if_required. Therefore, the order, in which the
     *             variables are passed into the vector must be the same for
     *             both functions.
     */
    void
    save_current_state_if_required(const std::function<void()> &save_state);

    /**
     * @brief      Reloads the previously stored variables in case of an implicit
     *             coupling. The current implementation supports subcycling,
     *             i.e. previously refers o the last time
     *             @p save_current_state_if_required() has been called.
     *
     * @param[out] state_variables Vector containing all variables to reload
     *             as reference
     *
     * @note       This function only makes sense, if the state variables have been
     *             stored by calling @p save_current_state_if_required. Therefore,
     *             the order, in which the variables are passed into the
     *             vector must be the same for both functions.
     */
    void
    reload_old_state_if_required(const std::function<void()> &reload_old_state);

    /**
     * @brief Public API adapter method, which calls the respective implementation
     *        in derived classes of the CouplingInterface. Have a look at the
     *        documentation there.
     */
    Tensor<1, dim, VectorizedArrayType>
    read_on_quadrature_point(const unsigned int id_number,
                             const unsigned int active_faces) const;

    /**
     * @brief is_coupling_ongoing Calls the preCICE API function isCouplingOnGoing
     *
     * @return returns true if the coupling has not yet been finished
     */
    bool
    is_coupling_ongoing() const;

    /**
     * @brief is_time_window_complete Calls the preCICE API function isTimeWindowComplete
     *
     * @return returns true if the coupling time window has been completed in the current
     *         iteration
     */
    bool
    is_time_window_complete() const;


    /// Boundary ID of the deal.II mesh, associated with the coupling
    /// interface. The variable is public and should be used during grid
    /// generation, but is also involved during system assembly. The only thing,
    /// one needs to make sure is, that this ID is not given to another part of
    /// the boundary e.g. clamped one.
    const unsigned int dealii_boundary_interface_id;


  private:
    // public precice solverinterface, needed in order to steer the time loop
    // inside the solver.
    std::shared_ptr<precice::SolverInterface> precice;
    /// The objects handling reading and writing data
    std::shared_ptr<CouplingInterface<dim, VectorizedArrayType>> writer;
    std::shared_ptr<CouplingInterface<dim, VectorizedArrayType>> reader;

    // Container to store time dependent data in case of an implicit coupling
    std::vector<VectorType> old_state_data;
    double                  old_time_value = 0;
  };



  template <int dim,
            int fe_degree,
            typename VectorType,
            typename VectorizedArrayType>
  template <typename ParameterClass>
  Adapter<dim, fe_degree, VectorType, VectorizedArrayType>::Adapter(
    const ParameterClass &parameters,
    const unsigned int    dealii_boundary_interface_id,
    std::shared_ptr<MatrixFree<dim, double, VectorizedArrayType>> data,
    const unsigned int                                            dof_index,
    const unsigned int read_quad_index,
    const unsigned int write_quad_index)
    : dealii_boundary_interface_id(dealii_boundary_interface_id)
  {
    precice = std::make_shared<precice::SolverInterface>(
      parameters.participant_name,
      parameters.config_file,
      Utilities::MPI::this_mpi_process(MPI_COMM_WORLD),
      Utilities::MPI::n_mpi_processes(MPI_COMM_WORLD));

    AssertThrow(dim == precice->getDimensions(), ExcInternalError());
    AssertThrow(dim > 1, ExcNotImplemented());

    // The read interface is always the same
    reader = std::make_shared<
      dealiiInterface<dim, fe_degree, fe_degree + 1, VectorizedArrayType>>(
      data,
      precice,
      parameters.read_mesh_name,
      dealii_boundary_interface_id,
      dof_index,
      read_quad_index);

    const bool use_solver_mapping = true;
    if (use_solver_mapping == true)
      {
        writer = std::make_shared<ArbitraryInterface<dim, VectorizedArrayType>>(
          data,
          precice,
          parameters.write_mesh_name,
          dealii_boundary_interface_id);
      }
    else if (parameters.write_mesh_name == parameters.read_mesh_name)
      {
        writer = reader;
      }
    else
      {
        writer = std::make_shared<
          dealiiInterface<dim, fe_degree, fe_degree + 1, VectorizedArrayType>>(
          data,
          precice,
          parameters.write_mesh_name,
          dealii_boundary_interface_id,
          dof_index,
          write_quad_index);
      }
    reader->add_read_data(parameters.read_data_name);
    writer->add_write_data(parameters.write_data_name);

    Assert(reader.get() != nullptr, ExcInternalError());
    Assert(writer.get() != nullptr, ExcInternalError());
  }



  template <int dim,
            int fe_degree,
            typename VectorType,
            typename VectorizedArrayType>
  void
  Adapter<dim, fe_degree, VectorType, VectorizedArrayType>::initialize(
    const VectorType &dealii_to_precice)
  {
    writer->define_coupling_mesh();
    reader->define_coupling_mesh();

    // Initialize preCICE internally
    precice->initialize();

    // Only the writer needs potentially to process the coupling mesh, if the
    // mapping is carried out in the solver
    writer->process_coupling_mesh();

    // write initial writeData to preCICE if required
    if (precice->isActionRequired(precice::constants::actionWriteInitialData()))
      {
        writer->write_data(dealii_to_precice);

        precice->markActionFulfilled(
          precice::constants::actionWriteInitialData());
      }
    precice->initializeData();

    // Maybe, read block-wise and work with an AlignedVector since the read data
    // (forces) is multiple times required during the Newton iteration
    //    if (shared_memory_parallel && precice->isReadDataAvailable())
    //      precice->readBlockVectorData(read_data_id,
    //                                   read_nodes_ids.size(),
    //                                   read_nodes_ids.data(),
    //                                   read_data.data());
  }



  template <int dim,
            int fe_degree,
            typename VectorType,
            typename VectorizedArrayType>
  void
  Adapter<dim, fe_degree, VectorType, VectorizedArrayType>::advance(
    const VectorType &dealii_to_precice,
    const double      computed_timestep_length)
  {
    if (precice->isWriteDataRequired(computed_timestep_length))
      writer->write_data(dealii_to_precice);
    // Here, we need to specify the computed time step length and pass it to
    // preCICE
    precice->advance(computed_timestep_length);

    //    if (shared_memory_parallel && precice->isReadDataAvailable())
    //      precice->readBlockVectorData(read_data_id,
    //                                   read_nodes_ids.size(),
    //                                   read_nodes_ids.data(),
    //                                   read_data.data());
  }


  template <int dim,
            int fe_degree,
            typename VectorType,
            typename VectorizedArrayType>
  inline Tensor<1, dim, VectorizedArrayType>
  Adapter<dim, fe_degree, VectorType, VectorizedArrayType>::
    read_on_quadrature_point(const unsigned int id_number,
                             const unsigned int active_faces) const
  {
    return reader->read_on_quadrature_point(id_number, active_faces);
  }



  template <int dim,
            int fe_degree,
            typename VectorType,
            typename VectorizedArrayType>
  inline void
  Adapter<dim, fe_degree, VectorType, VectorizedArrayType>::
    save_current_state_if_required(const std::function<void()> &save_state)
  {
    // First, we let preCICE check, whether we need to store the variables.
    // Then, the data is stored in the class
    if (precice->isActionRequired(
          precice::constants::actionWriteIterationCheckpoint()))
      {
        save_state();
        precice->markActionFulfilled(
          precice::constants::actionWriteIterationCheckpoint());
      }
  }



  template <int dim,
            int fe_degree,
            typename VectorType,
            typename VectorizedArrayType>
  inline void
  Adapter<dim, fe_degree, VectorType, VectorizedArrayType>::
    reload_old_state_if_required(const std::function<void()> &reload_old_state)
  {
    // In case we need to reload a state, we just take the internally stored
    // data vectors and write then in to the input data
    if (precice->isActionRequired(
          precice::constants::actionReadIterationCheckpoint()))
      {
        reload_old_state();
        precice->markActionFulfilled(
          precice::constants::actionReadIterationCheckpoint());
      }
  }



  template <int dim,
            int fe_degree,
            typename VectorType,
            typename VectorizedArrayType>
  inline bool
  Adapter<dim, fe_degree, VectorType, VectorizedArrayType>::
    is_coupling_ongoing() const
  {
    return precice->isCouplingOngoing();
  }



  template <int dim,
            int fe_degree,
            typename VectorType,
            typename VectorizedArrayType>
  inline bool
  Adapter<dim, fe_degree, VectorType, VectorizedArrayType>::
    is_time_window_complete() const
  {
    return precice->isTimeWindowComplete();
  }
} // namespace Adapter
