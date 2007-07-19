// -*- mode: c++; mode: auto-fill; mode: flyspell-prog; -*-
// Author Tatsiana Klimkovich, DESY <mailto:tklimk@mail.desy.de>
// Version: $Id $
/*
 *   This source code is part of the Eutelescope package of Marlin.
 *   You are free to use this source files for your own development as
 *   long as it stays in a public research context. You are not
 *   allowed to use it for commercial purpose. You must put this
 *   header with author names in all development based on this file.
 *
 */

// built only if GEAR is used
#ifdef USE_GEAR

// eutelescope includes ".h" 
#include "EUTelLineFit.h"
#include "EUTelRunHeaderImpl.h"
#include "EUTelEventImpl.h"
#include "EUTELESCOPE.h"
#include "EUTelVirtualCluster.h"
#include "EUTelFFClusterImpl.h"
#include "EUTelExceptions.h"

// marlin includes ".h"
#include "marlin/Processor.h"
#include "marlin/Global.h"

// gear includes <.h>
#include <gear/GearMgr.h>
#include <gear/SiPlanesParameters.h>

// aida includes <.h>
#ifdef MARLIN_USE_AIDA
#include <marlin/AIDAProcessor.h>
#include <AIDA/IHistogramFactory.h>
#include <AIDA/IHistogram1D.h>
#include <AIDA/ITree.h>
#endif

// lcio includes <.h> 
#include <IMPL/LCCollectionVec.h>
#include <IMPL/TrackerHitImpl.h>

// system includes <>
#include <string>
#include <vector>
#include <algorithm>

using namespace std;
using namespace marlin;
using namespace gear;
using namespace eutelescope;

// definition of static members mainly used to name histograms
#ifdef MARLIN_USE_AIDA
std::string EUTelLineFit::_chi2XLocalname          = "Chi2XLocal";
std::string EUTelLineFit::_chi2YLocalname          = "Chi2YLocal";
std::string EUTelLineFit::_residualX1Localname     = "ResidualX1Local";
std::string EUTelLineFit::_residualX2Localname     = "ResidualX2Local";
std::string EUTelLineFit::_residualX3Localname     = "ResidualX3Local";
std::string EUTelLineFit::_residualY1Localname     = "ResidualY1Local";
std::string EUTelLineFit::_residualY2Localname     = "ResidualY2Local";
std::string EUTelLineFit::_residualY3Localname     = "ResidualY3Local";
#endif


EUTelLineFit::EUTelLineFit () : Processor("EUTelLineFit") {
  
  // modify processor description
  _description =
    "EUTelLineFit will fit a straight line";
  
  registerInputCollection(LCIO::TRACKERHIT,"HitCollectionName",
			  "Hit collection name",
			  _hitCollectionName, string ( "hit" ));
}

void EUTelLineFit::init() {
  // this method is called only once even when the rewind is active
  // usually a good idea to
  printParameters ();
  
  // set to zero the run and event counters
  _iRun = 0;
  _iEvt = 0;
  
  // check if Marlin was built with GEAR support or not
#ifndef USE_GEAR
  
  message<ERROR> ( "Marlin was not built with GEAR support." );
  message<ERROR> ( "You need to install GEAR and recompile Marlin with -DUSE_GEAR before continue.");
  
  // I'm thinking if this is the case of throwing an exception or
  // not. This is a really error and not something that can
  // exceptionally happens. Still not sure what to do
  exit(-1);
  
#else
  
  // check if the GEAR manager pointer is not null!
  if ( Global::GEAR == 0x0 ) {
    message<ERROR> ( "The GearMgr is not available, for an unknown reason." );
    exit(-1);
  }
  
  _siPlanesParameters  = const_cast<SiPlanesParameters* > (&(Global::GEAR->getSiPlanesParameters()));
  _siPlanesLayerLayout = const_cast<SiPlanesLayerLayout*> ( &(_siPlanesParameters->getSiPlanesLayerLayout() ));
  
  _histogramSwitch = true;
  
#endif
  
}

void EUTelLineFit::processRunHeader (LCRunHeader * rdr) {
  

  EUTelRunHeaderImpl * header = static_cast<EUTelRunHeaderImpl*> (rdr);
  
  // the run header contains the number of detectors. This number
  // should be in principle be the same as the number of layers in the
  // geometry description
  if ( header->getNoOfDetector() != _siPlanesParameters->getSiPlanesNumber() ) {
    message<ERROR> ( "Error during the geometry consistency check: " );
    message<ERROR> ( log() << "The run header says there are " << header->getNoOfDetector() << " silicon detectors " );
    message<ERROR> ( log() << "The GEAR description says     " << _siPlanesParameters->getSiPlanesNumber() << " silicon planes" );
    exit(-1);
  }
  
  // this is the right place also to check the geometry ID. This is a
  // unique number identifying each different geometry used at the
  // beam test. The same number should be saved in the run header and
  // in the xml file. If the numbers are different, instead of barely
  // quitting ask the user what to do.
  
  if ( header->getGeoID() != _siPlanesParameters->getSiPlanesID() ) {
    message<ERROR> ( "Error during the geometry consistency check: " );
    message<ERROR> ( log() << "The run header says the GeoID is " << header->getGeoID() );
    message<ERROR> ( log() << "The GEAR description says is     " << _siPlanesParameters->getSiPlanesNumber() );
    string answer;
    while (true) {
      message<ERROR> ( "Type Q to quit now or C to continue using the actual GEAR description anyway [Q/C]" );
      cin >> answer;
      // put the answer in lower case before making the comparison.
      transform( answer.begin(), answer.end(), answer.begin(), ::tolower );
      if ( answer == "q" ) {
   	exit(-1);
      } else if ( answer == "c" ) {
   	break;
      }
    }
  }
  
  // now book histograms plz...
  if ( isFirstEvent() )  bookHistos();
  
  // increment the run counter
  ++_iRun;
}


void EUTelLineFit::processEvent (LCEvent * event) {
  
  
  EUTelEventImpl * evt = static_cast<EUTelEventImpl*> (event) ;
  
  if ( evt->getEventType() == kEORE ) {
    message<DEBUG> ( "EORE found: nothing else to do." );
    return;
  }
  
  LCCollectionVec * hitCollection     = static_cast<LCCollectionVec*> (event->getCollection( _hitCollectionName ));
  
  int detectorID    = -99; // it's a non sense
  int oldDetectorID = -100;

  _nPlanes = _siPlanesParameters->getSiPlanesNumber();

  _xPos = new double[_nPlanes];
  _yPos = new double[_nPlanes];
  _zPos = new double[_nPlanes];
  _waferResidX = new double[_nPlanes];
  _waferResidY = new double[_nPlanes];
  
  int    layerIndex; 
  _intrResolX = new double[_nPlanes];
  _intrResolY = new double[_nPlanes];
  
  for ( int iHit = 0; iHit < hitCollection->getNumberOfElements(); iHit++ ) {
    
    TrackerHitImpl * hit = static_cast<TrackerHitImpl*> ( hitCollection->getElementAt(iHit) );
    
    LCObjectVec clusterVector = hit->getRawHits();
    
    EUTelVirtualCluster * cluster;
    if ( hit->getType() == kEUTelFFClusterImpl ) {
      cluster = new EUTelFFClusterImpl( static_cast<TrackerDataImpl *> ( clusterVector[0] ) );
    } else {
      throw UnknownDataTypeException("Unknown cluster type");
    }
    
    detectorID = cluster->getDetectorID();
    
    if ( detectorID != oldDetectorID ) {
      oldDetectorID = detectorID;
      
      if ( _conversionIdMap.size() != (unsigned) _siPlanesParameters->getSiPlanesNumber() ) {
	// first of all try to see if this detectorID already belong to 
	if ( _conversionIdMap.find( detectorID ) == _conversionIdMap.end() ) {
	  // this means that this detector ID was not already inserted,
	  // so this is the right place to do that
	  for ( int iLayer = 0; iLayer < _siPlanesLayerLayout->getNLayers(); iLayer++ ) {
	    if ( _siPlanesLayerLayout->getID(iLayer) == detectorID ) {
	      _conversionIdMap.insert( make_pair( detectorID, iLayer ) );
	      break;
	    }
	  }
	}
      }
      
      // here we take intrinsic resolution from geometry database

      layerIndex   = _conversionIdMap[detectorID];     
      _intrResolX[layerIndex] = 1000*_siPlanesLayerLayout->getSensitiveResolution(layerIndex); //um
      _intrResolY[layerIndex] = 1000*_siPlanesLayerLayout->getSensitiveResolution(layerIndex); //um
      
    }
    
    _xPos[iHit] = 1000 * hit->getPosition()[0];
    _yPos[iHit] = 1000 * hit->getPosition()[1];
    _zPos[iHit] = 1000 * hit->getPosition()[2];
    
  }
  
  // ++++++++++++++++++++++++++++++++++++++++++++++++++++++
  // ++++++++++++ See Blobel Page 226 !!! ++++++++++++++
  // ++++++++++++++++++++++++++++++++++++++++++++++++++++++
  
  int counter;
  
  float S1[2]   = {0,0};
  float Sx[2]   = {0,0};
  float Xbar[2] = {0,0};
  
  float Zbar_X[_nPlanes];
  float Zbar_Y[_nPlanes];
  for (counter = 0; counter < _nPlanes; counter++){
    Zbar_X[counter] = 0.;
    Zbar_Y[counter] = 0.;
  }
  
  float Sy[2]     = {0,0};
  float Ybar[2]   = {0,0};
  float Sxybar[2] = {0,0};
  float Sxxbar[2] = {0,0};
  float A2[2]     = {0,0};
  float Chiquare[2] = {0,0};
  
  
  // define S1
  for( counter = 0; counter < _nPlanes; counter++ ){
    S1[0] = S1[0] + 1/pow(_intrResolX[counter],2);
    S1[1] = S1[1] + 1/pow(_intrResolY[counter],2);
  }
  
  // define Sx
  for( counter = 0; counter < _nPlanes; counter++ ){
    Sx[0] = Sx[0] + _zPos[counter]/pow(_intrResolX[counter],2);
    Sx[1] = Sx[1] + _zPos[counter]/pow(_intrResolY[counter],2);
  }
  
  // define Xbar
  Xbar[0]=Sx[0]/S1[0];
  Xbar[1]=Sx[1]/S1[1];
  
  // coordinate transformation !! -> bar
  for( counter=0; counter < _nPlanes; counter++ ){
    Zbar_X[counter] = _zPos[counter]-Xbar[0];
    Zbar_Y[counter] = _zPos[counter]-Xbar[1];
  } 
  
  // define Sy
  for( counter = 0; counter < _nPlanes; counter++ ){
    Sy[0] = Sy[0] + _xPos[counter]/pow(_intrResolX[counter],2);
    Sy[1] = Sy[1] + _yPos[counter]/pow(_intrResolY[counter],2);
  }
  
  // define Ybar
  Ybar[0]=Sy[0]/S1[0];
  Ybar[1]=Sy[1]/S1[1];
  
  // define Sxybar
  for( counter = 0; counter < _nPlanes; counter++ ){
    Sxybar[0] = Sxybar[0] + Zbar_X[counter] * _xPos[counter]/pow(_intrResolX[counter],2);
    Sxybar[1] = Sxybar[1] + Zbar_Y[counter] * _yPos[counter]/pow(_intrResolY[counter],2);
  }
  
  // define Sxxbar
  for( counter = 0; counter < _nPlanes; counter++ ){
    Sxxbar[0] = Sxxbar[0] + Zbar_X[counter] * Zbar_X[counter]/pow(_intrResolX[counter],2);
    Sxxbar[1] = Sxxbar[1] + Zbar_Y[counter] * Zbar_Y[counter]/pow(_intrResolX[counter],2);
  }
  
  // define A2
  
  A2[0]=Sxybar[0]/Sxxbar[0];
  A2[1]=Sxybar[1]/Sxxbar[1];
  
  // Calculate chi sqaured
  // Chi^2 for X coordinate for hits in all planes 
  
  for( counter = 0; counter < _nPlanes; counter++ ){
    Chiquare[0] += pow(-_zPos[counter]*A2[0]
		       +_xPos[counter]-Ybar[0]+Xbar[0]*A2[0],2)/pow(_intrResolX[counter],2);
  }
  
  // Chi^2 for Y coordinate for hits in all planes 
  
  for( counter = 0; counter < _nPlanes; counter++ ){
    Chiquare[1] += pow(-_zPos[counter]*A2[1]
		       +_yPos[counter]-Ybar[1]+Xbar[1]*A2[1],2)/pow(_intrResolY[counter],2);
  }
  
  //     cout << "Chiquare[0] = " << Chiquare[0] << endl;
  //     cout << "Chiquare[1] = " << Chiquare[1] << endl;
  
  for( counter = 0; counter < _nPlanes; counter++ ){
    _waferResidX[counter] = (Ybar[0]-Xbar[0]*A2[0]+_zPos[counter]*A2[0])-_xPos[counter];
    _waferResidY[counter] = (Ybar[1]-Xbar[1]*A2[1]+_zPos[counter]*A2[1])-_yPos[counter];
  }
  
  
#ifdef MARLIN_USE_AIDA
  string tempHistoName;
  
  if ( _histogramSwitch ) {
    {
      stringstream ss; 
      ss << _chi2XLocalname << endl;
    }
    if ( AIDA::IHistogram1D* chi2x_histo = dynamic_cast<AIDA::IHistogram1D*>(_aidaHistoMap[_chi2XLocalname]) )
      chi2x_histo->fill(Chiquare[0]);
    else {
      message<ERROR> ( log() << "Not able to retrieve histogram pointer for " <<  _chi2XLocalname
		       << ".\nDisabling histogramming from now on " );
      _histogramSwitch = false;
    }       
  }
  
  if ( _histogramSwitch ) {
    {
      stringstream ss; 
      ss << _chi2YLocalname << endl;
    }
    if ( AIDA::IHistogram1D* chi2y_histo = dynamic_cast<AIDA::IHistogram1D*>(_aidaHistoMap[_chi2YLocalname]) )
      chi2y_histo->fill(Chiquare[1]);
    else {
      message<ERROR> ( log() << "Not able to retrieve histogram pointer for " <<  _chi2YLocalname
		       << ".\nDisabling histogramming from now on " );
      _histogramSwitch = false;
    }       
  }
  
  if ( _histogramSwitch ) {
    {
      stringstream ss; 
      ss << _residualX1Localname << endl;
    }
    if ( AIDA::IHistogram1D* residx1_histo = dynamic_cast<AIDA::IHistogram1D*>(_aidaHistoMap[_residualX1Localname]) )
      residx1_histo->fill(_waferResidX[0]);
    else {
      message<ERROR> ( log() << "Not able to retrieve histogram pointer for " <<  _residualX1Localname
		       << ".\nDisabling histogramming from now on " );
      _histogramSwitch = false;
    }       
  }
  
  if ( _histogramSwitch ) {
    if ( AIDA::IHistogram1D* residx2_histo = dynamic_cast<AIDA::IHistogram1D*>(_aidaHistoMap[_residualX2Localname]) )
      residx2_histo->fill(_waferResidX[1]);
    else {
      message<ERROR> ( log() << "Not able to retrieve histogram pointer for " <<  _residualX2Localname
		       << ".\nDisabling histogramming from now on " );
      _histogramSwitch = false;
    }
  }
  
  if ( _histogramSwitch ) {
    if ( AIDA::IHistogram1D* residx3_histo = dynamic_cast<AIDA::IHistogram1D*>(_aidaHistoMap[_residualX3Localname]) )
      residx3_histo->fill(_waferResidX[2]);
    else {
      message<ERROR> ( log() << "Not able to retrieve histogram pointer for " <<  _residualX3Localname
		       << ".\nDisabling histogramming from now on " );
      _histogramSwitch = false;
    }
  }
  
  if ( _histogramSwitch ) {
    if ( AIDA::IHistogram1D* residy1_histo = dynamic_cast<AIDA::IHistogram1D*>(_aidaHistoMap[_residualY1Localname]) )
      residy1_histo->fill(_waferResidY[0]);
    else {
      message<ERROR> ( log() << "Not able to retrieve histogram pointer for " <<  _residualY1Localname
		       << ".\nDisabling histogramming from now on " );
      _histogramSwitch = false;
    }
  }
  
  if ( _histogramSwitch ) {
    if ( AIDA::IHistogram1D* residy2_histo = dynamic_cast<AIDA::IHistogram1D*>(_aidaHistoMap[_residualY2Localname]) )
      residy2_histo->fill(_waferResidY[1]);
    else {
      message<ERROR> ( log() << "Not able to retrieve histogram pointer for " <<  _residualY2Localname
		       << ".\nDisabling histogramming from now on " );
      _histogramSwitch = false;
    }
  }       
  
  if ( _histogramSwitch ) {
    if ( AIDA::IHistogram1D* residy3_histo = dynamic_cast<AIDA::IHistogram1D*>(_aidaHistoMap[_residualY3Localname]) )
      residy3_histo->fill(_waferResidY[2]);
    else {
      message<ERROR> ( log() << "Not able to retrieve histogram pointer for " <<  _residualY3Localname
		       << ".\nDisabling histogramming from now on " );
      _histogramSwitch = false;
    }
  }
  
#endif 
  
  ++_iEvt;
  
  if ( isFirstEvent() ) _isFirstEvent = false;
  
}

void EUTelLineFit::end() {
  
  delete [] _xPos;
  delete [] _yPos;
  delete [] _zPos;
  delete [] _waferResidX;
  delete [] _waferResidY;
  delete [] _intrResolX;
  delete [] _intrResolY;

  message<MESSAGE> ( log() << "Successfully finished" ) ;  

}

void EUTelLineFit::bookHistos() {
  
  
#ifdef MARLIN_USE_AIDA
  
  try {
    message<MESSAGE> ( "Booking histograms" );
    
    string tempHistoName;
    
    {
      stringstream ss ;
      ss <<   _residualX1Localname ;
      tempHistoName = ss.str();
      //	cout << "tempHistoName in bookHistos = " << tempHistoName << endl; 
    }
    
    const int    NBin = 100;
    const double Chi2Min  = 0.;      
    const double Chi2Max  = 10000.;      
    const double Min  = -500.;
    const double Max  = 500.;
    
    AIDA::IHistogram1D * chi2XLocal = 
      AIDAProcessor::histogramFactory(this)->createHistogram1D(_chi2XLocalname,NBin,Chi2Min,Chi2Max);
    if ( chi2XLocal ) {
      chi2XLocal->setTitle("Chi2 X");
      _aidaHistoMap.insert( make_pair( _chi2XLocalname, chi2XLocal ) );
    } else {
      message<ERROR> ( log() << "Problem booking the " << (_chi2XLocalname) << ".\n"
		       << "Very likely a problem with path name. Switching off histogramming and continue w/o");
      _histogramSwitch = false;
    }
    
    AIDA::IHistogram1D * chi2YLocal = 
      AIDAProcessor::histogramFactory(this)->createHistogram1D(_chi2YLocalname,NBin,Chi2Min,Chi2Max);
    if ( chi2YLocal ) {
      chi2YLocal->setTitle("Chi2 Y");
      _aidaHistoMap.insert( make_pair( _chi2YLocalname, chi2YLocal ) );
    } else {
      message<ERROR> ( log() << "Problem booking the " << (_chi2YLocalname) << ".\n"
		       << "Very likely a problem with path name. Switching off histogramming and continue w/o");
      _histogramSwitch = false;
    }
    
    AIDA::IHistogram1D * residualX1Local = 
      AIDAProcessor::histogramFactory(this)->createHistogram1D(_residualX1Localname,NBin, Min,Max);
    if ( residualX1Local ) {
      residualX1Local->setTitle("X Residual 1");
      _aidaHistoMap.insert( make_pair( _residualX1Localname, residualX1Local ) );
    } else {
      message<ERROR> ( log() << "Problem booking the " << (_residualX1Localname) << ".\n"
		       << "Very likely a problem with path name. Switching off histogramming and continue w/o");
      _histogramSwitch = false;
    }
    
    AIDA::IHistogram1D * residualX2Local = 
      AIDAProcessor::histogramFactory(this)->createHistogram1D(_residualX2Localname,NBin, Min,Max);
    if ( residualX2Local ) {
      residualX2Local->setTitle("X Residual 2");
      _aidaHistoMap.insert( make_pair( _residualX2Localname, residualX2Local ) );
    } else {
      message<ERROR> ( log() << "Problem booking the " << (_residualX1Localname) << ".\n"
		       << "Very likely a problem with path name. Switching off histogramming and continue w/o");
      _histogramSwitch = false;
    }
    
    AIDA::IHistogram1D * residualX3Local = 
      AIDAProcessor::histogramFactory(this)->createHistogram1D(_residualX3Localname,NBin, Min,Max);
    if ( residualX3Local ) {
      residualX3Local->setTitle("X Residual 3");
      _aidaHistoMap.insert( make_pair( _residualX3Localname, residualX3Local ) );
    } else {
      message<ERROR> ( log() << "Problem booking the " << (_residualX1Localname) << ".\n"
		       << "Very likely a problem with path name. Switching off histogramming and continue w/o");
      _histogramSwitch = false;
    }
    
    AIDA::IHistogram1D * residualY1Local = 
      AIDAProcessor::histogramFactory(this)->createHistogram1D(_residualY1Localname,NBin, Min,Max);
    if ( residualY1Local ) {
      residualY1Local->setTitle("Y Residual 1");
      _aidaHistoMap.insert( make_pair( _residualY1Localname, residualY1Local ) );
    } else {
      message<ERROR> ( log() << "Problem booking the " << (_residualX1Localname) << ".\n"
		       << "Very likely a problem with path name. Switching off histogramming and continue w/o");
      _histogramSwitch = false;
    }
    
    AIDA::IHistogram1D * residualY2Local = 
      AIDAProcessor::histogramFactory(this)->createHistogram1D(_residualY2Localname,NBin, Min,Max);
    if ( residualY2Local ) {
      residualY2Local->setTitle("Y Residual 2");
      _aidaHistoMap.insert( make_pair( _residualY2Localname, residualY2Local ) );
    } else {
      message<ERROR> ( log() << "Problem booking the " << (_residualX1Localname) << ".\n"
		       << "Very likely a problem with path name. Switching off histogramming and continue w/o");
      _histogramSwitch = false;
    }
    
    AIDA::IHistogram1D * residualY3Local = 
      AIDAProcessor::histogramFactory(this)->createHistogram1D(_residualY3Localname,NBin, Min,Max);
    if ( residualY3Local ) {
      residualY3Local->setTitle("Y Residual 3");
      _aidaHistoMap.insert( make_pair( _residualY3Localname, residualY3Local ) );
    } else {
      message<ERROR> ( log() << "Problem booking the " << (_residualX1Localname) << ".\n"
		       << "Very likely a problem with path name. Switching off histogramming and continue w/o");
      _histogramSwitch = false;
    }
    
  } catch (lcio::Exception& e ) {
    
    message<ERROR> ( log() << "No AIDAProcessor initialized. Type q to exit or c to continue without histogramming" );
    string answer;
    while ( true ) {
      message<ERROR> ( "[q]/[c]" );
      cin >> answer;
      transform( answer.begin(), answer.end(), answer.begin(), ::tolower );
      if ( answer == "q" ) {
	exit(-1);
      } else if ( answer == "c" )
	_histogramSwitch = false;
      break;
    }
    
  }
#endif
  
}

  
#endif
