/******************************************************************************
  This source file is part of the Avogadro project.
  This source code is released under the 3-Clause BSD License, (see "LICENSE").
******************************************************************************/

#include "gaussiancube.h"

#include <avogadro/core/cube.h>
#include <avogadro/core/molecule.h>
#include <avogadro/core/utilities.h>

#include <iostream>
#include <iomanip>

namespace Avogadro::QuantumIO {

GaussianCube::GaussianCube() {}

GaussianCube::~GaussianCube() {}

std::vector<std::string> GaussianCube::fileExtensions() const
{
  std::vector<std::string> extensions;
  extensions.emplace_back("cube");
  return extensions;
}

std::vector<std::string> GaussianCube::mimeTypes() const
{
  return std::vector<std::string>();
}

bool GaussianCube::read(std::istream& in, Core::Molecule& molecule)
{
  // Variables we will need
  std::string line;
  std::vector<std::string> list;

  int nAtoms;
  Vector3 min;
  Vector3 spacing;
  Vector3i dim;

  // Gaussian Cube format is very specific

  // Read and set name
  getline(in, line);
  molecule.setData("name", line);

  // Read and skip field title (we may be able to use this to setCubeType in the
  // future)
  getline(in, line);

  // Next line contains nAtoms and m_min
  in >> nAtoms;
  for (unsigned int i = 0; i < 3; ++i)
    in >> min(i);
  getline(in, line); // capture newline before continuing

  // Next 3 lines contains spacing and dim
  for (unsigned int i = 0; i < 3; ++i) {
    getline(in, line);
    line = Core::trimmed(line);
    list = Core::split(line, ' ');
    dim(i) = Core::lexicalCast<int>(list[0]);
    spacing(i) = Core::lexicalCast<double>(list[i + 1]);
  }

  // Geometry block
  Vector3 pos;
  for (int i = 0; i < abs(nAtoms); ++i) {
    getline(in, line);
    line = Core::trimmed(line);
    list = Core::split(line, ' ');
    auto atomNum = Core::lexicalCast<short int>(list[0]);
    Core::Atom a = molecule.addAtom(static_cast<unsigned char>(atomNum));
    for (unsigned int j = 2; j < 5; ++j)
      pos(j - 2) = Core::lexicalCast<double>(list[j]);
    pos = pos * BOHR_TO_ANGSTROM;
    a.setPosition3d(pos);
  }

  // If the nAtoms were negative there is another line before
  // the data which is necessary, maybe contain 1 or more cubes
  unsigned int nCubes = 1;
  if (nAtoms < 0) {
    in >> nCubes;
    std::vector<unsigned int> moList(nCubes);
    for (unsigned int i = 0; i < nCubes; ++i)
      in >> moList[i];
    // clear buffer
    getline(in, line);
  }

  // Render molecule
  molecule.perceiveBondsSimple();

  // Cube block, set limits and populate data
  // min and spacing are in bohr units, convert to ANGSTROM
  min *= BOHR_TO_ANGSTROM;
  spacing *= BOHR_TO_ANGSTROM;

  for (unsigned int i = 0; i < nCubes; ++i) {
    // Get a cube object from molecule
    Core::Cube* cube = molecule.addCube();

    cube->setLimits(min, dim, spacing);
    std::vector<float> values;
    // push_back is slow for this, resize vector first
    values.resize(dim(0) * dim(1) * dim(2));
    for (float & value : values)
      in >> value;
    // clear buffer, if more than one cube
    getline(in, line);
    cube->setData(values);
  }

  return true;
}

void writeFixedFloat(std::ostream& outStream, Real number)
{
  outStream << std::setw(12) << std::fixed << std::right 
    << std::setprecision(6) << number;
}

void writeFixedInt(std::ostream& outStream, unsigned int number)
{
  outStream << std::setw(5) << std::fixed << std::right
            << number;
}

bool GaussianCube::write(std::ostream& outStream, const Core::Molecule& mol)
{
  if (mol.cubeCount() == 0)
    return false; // no cubes to write

  const Core::Cube* cube = mol.cube(0); // eventually need to write all the cubes
  Vector3 min = cube->min() * ANGSTROM_TO_BOHR;
  Vector3 spacing = cube->spacing() * ANGSTROM_TO_BOHR;
  Vector3i dim = cube->dimensions(); // number of points in each direction

  // might be useful to use the 2nd line, but it's just a comment
  // e.g. write out the cube type
  outStream << "Gaussian Cube file generated by Avogadro.\n";
  if (mol.data("name").toString().length())
    outStream << mol.data("name").toString() << "\n";
  else
    outStream << "\n";

  // Write out the number of atoms and the minimum coordinates
  size_t numAtoms = mol.atomCount();
  writeFixedInt(outStream, numAtoms);
  writeFixedFloat(outStream,min[0]);
  writeFixedFloat(outStream,min[1]);
  writeFixedFloat(outStream,min[2]);
  writeFixedInt(outStream,1); // one value per point (i.e., not vector)
  outStream << "\n";

  // now write the size and spacing of the cube
  writeFixedInt(outStream,dim[0]);
  writeFixedFloat(outStream,spacing[0]);
  writeFixedFloat(outStream,0.0);
  writeFixedFloat(outStream,0.0);
  outStream << "\n";

  writeFixedInt(outStream,dim[1]);
  writeFixedFloat(outStream,0.0);
  writeFixedFloat(outStream,spacing[1]);
  writeFixedFloat(outStream,0.0);
  outStream << "\n";

  writeFixedInt(outStream,dim[2]);
  writeFixedFloat(outStream,0.0);
  writeFixedFloat(outStream,0.0);
  writeFixedFloat(outStream,spacing[2]);
  outStream << "\n";

  for (size_t i = 0; i < numAtoms; ++i) {
    Core::Atom atom = mol.atom(i);
    if (!atom.isValid()) {
      appendError("Internal error: Atom invalid.");
      return false;
    }

    writeFixedInt(outStream,static_cast<int>(atom.atomicNumber()));
    writeFixedFloat(outStream,0.0); // charge
    writeFixedFloat(outStream,atom.position3d()[0] * ANGSTROM_TO_BOHR);
    writeFixedFloat(outStream,atom.position3d()[1] * ANGSTROM_TO_BOHR);
    writeFixedFloat(outStream,atom.position3d()[2] * ANGSTROM_TO_BOHR);
    outStream << "\n";
  }

  // write the raw cube values
  const std::vector<float>* values = cube->data();
  for (unsigned int i = 0; i < values->size(); ++i) {
    outStream << std::setw(13) << std::right << std::scientific 
              << std::setprecision(5) << (*values)[i];
    if (i % 6 == 5)
      outStream << "\n";
  }

  return true;
}

} // namespace Avogadro
