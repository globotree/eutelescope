#include "EUTelProcessorTrackingExhaustiveTrackSearch.h"

// C++
#include <map>
#include <memory>
#include <string>
#include <vector>

// LCIO
#include <EVENT/LCCollection.h>

// MARLIN
#include "marlin/Exceptions.h"
#include "marlin/Global.h"
#include "marlin/Processor.h"
#include "marlin/VerbosityLevels.h"

// AIDA
#ifdef MARLIN_USE_AIDA
#include <marlin/AIDAProcessor.h>
#include <AIDA/IHistogramFactory.h>
#include <AIDA/IHistogram1D.h>
#endif // MARLIN_USE_AIDA

// EUTELESCOPE
#include "EUTelExceptions.h"
#include "EUTelRunHeaderImpl.h"
#include "EUTelEventImpl.h"
#include "EUTelUtility.h"
#include "EUTelExhaustiveTrackFinder.h"
#include "EUTelGeometryTelescopeGeoDescription.h"
// Cluster types
#include "EUTelSparseCluster2Impl.h"
#include "EUTelSparseClusterImpl.h"
#include "EUTelBrickedClusterImpl.h"
#include "EUTelDFFClusterImpl.h"
#include "EUTelFFClusterImpl.h"


using namespace lcio;
using namespace marlin;
using namespace eutelescope;

/**  EUTelProcessorTrackingExhaustiveTrackSearch
 * 
 *  If compiled with MARLIN_USE_AIDA 
 *  it creates histograms.
 * 
 *  <h4>Input - Prerequisites</h4>
 *  Requires collection of hits from which track
 *  candidates are build.
 *
 *  <h4>Output</h4> 
 *  <li> Histograms.
 *  <li> Collection of track candidates.
 */

// definition of static members mainly used to name histograms
#if defined(USE_AIDA) || defined(MARLIN_USE_AIDA)
std::string EUTelProcessorTrackingExhaustiveTrackSearch::_numberTracksCandidatesHistName = "NumberTracksCandidates";
#endif // defined(USE_AIDA) || defined(MARLIN_USE_AIDA)

/** Default constructor */
EUTelProcessorTrackingExhaustiveTrackSearch::EUTelProcessorTrackingExhaustiveTrackSearch( ) :
 Processor( "EUTelProcessorTrackingExhaustiveTrackSearch" ),
 _hitInputCollectionName( "HitCollection" ),
 _trackCandidateHitsOutputCollectionName( "TrackCandidatesCollection" ),
 _nProcessedRuns( 0 ),
 _nProcessedEvents( 0 ) {

    // Processor description
    _description = "EUTelProcessorTrackingExhaustiveTrackSearch preforms track pattern recognition.";

    // TrackerHit input collection
    registerInputCollection( LCIO::TRACKERHIT,
            "HitInputCollectionName",
            "Input hits collection name",
            _hitInputCollectionName,
            std::string( "HitCollection" ) );

    // Track candidate hits output collection
    registerOutputCollection( LCIO::TRACKERHIT,
            "TrackCandHitOutputCollectionName",
            "Output track candidates hits collection name",
            _trackCandidateHitsOutputCollectionName,
            std::string( "TrackCandidateHitCollection" ) );


    // Optinal processor parameters that define finder settings

    registerOptionalParameter( "MaxMissingHitsPerTrack", "Maximal number of missing hits on a track candidate",
            _maxMissingHitsPerTrackCand, static_cast < int > ( 0 ) ); // Search full-length tracks by default

    registerOptionalParameter( "MaxNTracksPerEvent", "Maximal number of track candidates to be found in events",
            _maxNTracks, static_cast < int > ( 100 ) );

    registerOptionalParameter( "FinderMode", "Finder mode. Possible values are 1, 2",
            _finderMode, static_cast < int > ( 2 ) );

    const FloatVec MinimalResidualsX( 10, -3000. ); // assumes there are at most 6 planes. But this doesn't matter.
    registerOptionalParameter( "ResidualsXMin",
            "Minimal values of the hit residuals in the X direction for a track. "
            "Note: these numbers are ordered according to the z position of "
            "the sensors and NOT according to the sensor id.",
            _residualsXMin, MinimalResidualsX );

    const FloatVec MinimalResidualsY( 10, -3000. ); // assumes there are at most 6 planes. But this doesn't matter.
    registerOptionalParameter( "ResidualsYMin", "Minimal values of the hit residuals in the Y direction for a track. "
            "Note: these numbers are ordered according to the z position of "
            "the sensors and NOT according to the sensor id.",
            _residualsYMin, MinimalResidualsY );

    const FloatVec MaximalResidualsX( 10, 3000. ); // assumes there are at most 6 planes. But this doesn't matter.
    registerOptionalParameter( "ResidualsXMax", "Maximal values of the hit residuals in the X direction for a track. "
            "Note: these numbers are ordered according to the z position of "
            "the sensors and NOT according to the sensor id.",
            _residualsXMax, MaximalResidualsX );

    const FloatVec MaximalResidualsY( 10, 3000. ); // assumes there are at most 6 planes. But this doesn't matter.
    registerOptionalParameter( "ResidualsYMax", "Maximal values of the hit residuals in the Y direction for a track. "
            "Note: these numbers are ordered according to the z position of "
            "the sensors and NOT according to the sensor id.",
            _residualsYMax, MaximalResidualsY );

    const FloatVec MaximalResidualsR( 10, 3000. );
    registerOptionalParameter( "ResidualsRMax", "Maximal allowed distance between hits entering the recognition step "
            "per 10 cm space between the planes. One value for each neighbor planes. "
            "DistanceMax will be used for each pair if this vector is empty.",
            _residualsRMax, MaximalResidualsR );


    /**    @TODO must be a part of a separate data structure 
     *     Do we need this at all
     */
    registerOptionalParameter( "HotPixelCollectionName", "Name of the hot pixel collection ",
            _hotPixelCollectionName, static_cast < std::string > ( "hotpixel_apix" ) );

    registerOptionalParameter( "MimosaClusterChargeMin", "Remove Mimosa26 clusters with a charge "
            "(i.e. number of fired pixels in cluster) below or equal to this value",
            _mimosa26ClusterChargeMin, static_cast < int > ( 1 ) );
}

void EUTelProcessorTrackingExhaustiveTrackSearch::init( ) {

    streamlog_out( DEBUG ) << "EUTelProcessorTrackingExhaustiveTrackSearch::init( )" << std::endl;

    // usually a good idea to
    printParameters( );

    // Reset counters
    _nProcessedRuns = 0;
    _nProcessedEvents = 0;


    // Getting access to geometry description
    _geometry = new EUTelGeometryTelescopeGeoDescription;

    // Instantiate track finder. This is a working horse of the processor.
    {
        streamlog_out( DEBUG ) << "Initialisation of track finder" << std::endl;

        EUTelExhaustiveTrackFinder* Finder = new EUTelExhaustiveTrackFinder( "TrackFinder", getAllowedMissingHits( ), getMaxTrackCandidates( ) );
        if ( !Finder ) {
            streamlog_out( ERROR ) << "Can't allocate an instance of EUTelExhaustiveTrackFinder. Stopping ..." << std::endl;
            throw UnknownDataTypeException( "Track finder was not created" );
        }
        Finder->SetMode( getFinderMode( ) );
        Finder->SetDistanceMaxVec( _residualsRMax );
        Finder->SetResidualsYMax( _residualsYMax );
        Finder->SetResidualsYMin( _residualsYMin );
        Finder->SetResidualsXMax( _residualsXMax );
        Finder->SetResidualsXMin( _residualsXMin );

        _trackFinder = Finder;
    }

    // Book histograms
    bookHistograms( );
}

void EUTelProcessorTrackingExhaustiveTrackSearch::processRunHeader( LCRunHeader* run ) {

    auto_ptr<EUTelRunHeaderImpl> header( new EUTelRunHeaderImpl( run ) );
    header->addProcessor( type( ) );


    // this is the right place also to check the geometry ID. This is a
    // unique number identifying each different geometry used at the
    // beam test. The same number should be saved in the run header and
    // in the xml file. If the numbers are different, warn the user.

    if ( header->getGeoID( ) == 0 )
        streamlog_out( WARNING0 ) << "The geometry ID in the run header is set to zero." << endl
            << "This may mean that the GeoID parameter was not set" << endl;


    if ( header->getGeoID( ) != _geometry->_siPlanesParameters->getSiPlanesID( ) ) {
        streamlog_out( WARNING5 ) << "Error during the geometry consistency check: " << endl
                << "The run header says the GeoID is " << header->getGeoID( ) << endl
                << "The GEAR description says is     " << _geometry->_siPlanesParameters->getSiPlanesID( ) << endl;
    }

    _nProcessedRuns++;
}

void EUTelProcessorTrackingExhaustiveTrackSearch::processEvent( LCEvent * evt ) {

    if ( isFirstEvent( ) ) {
        // Fill hot pixel map   
        _hotPixelMap = Utility::FillHotPixelMap( evt, _hotPixelCollectionName );
    }

    EUTelEventImpl * event = static_cast < EUTelEventImpl* > ( evt );

    // Do not process last events
    if ( event->getEventType( ) == kEORE ) {
        streamlog_out( DEBUG2 ) << "EORE found: nothing else to do." << std::endl;
        return;
    }

    // Try to access collection

    LCCollection* col = NULL;
    try {
        col = evt->getCollection( _hitInputCollectionName );
    } catch ( DataNotAvailableException e ) {
        streamlog_out( WARNING ) << _hitInputCollectionName << " collection not available" << std::endl;
        throw marlin::SkipEventException( this );
    }

    // this will only be entered if the collection is available
    if ( col != NULL ) {
        if ( _nProcessedEvents % 10000 == 1 ) streamlog_out( MESSAGE5 ) << "EUTelProcessorTrackingExhaustiveTrackSearch" << endl;

        // Prepare hits for track finder
        vector< EVENT::TrackerHitVec > allHitsArray( _geometry->_nPlanes, EVENT::TrackerHitVec( ) );
        FillHits( evt, col, allHitsArray );

        // Search tracks
        int _nTracks = 0;
        int _nGoodTracks = 0;

        std::map< int, EVENT::TrackerHitVec > trackCandidates;

        streamlog_out( DEBUG1 ) << "Event #" << _nProcessedEvents << std::endl;
        streamlog_out( DEBUG1 ) << "Initialising hits for _theFinder..." << std::endl;
        TrackerHitVec::iterator itHit;
        for ( size_t iLayer = 0; iLayer < _geometry->_nPlanes; iLayer++ ) {
            streamlog_out( DEBUG1 ) << iLayer << endl;
            for ( itHit = allHitsArray[iLayer].begin( ); itHit != allHitsArray[iLayer].end( ); ++itHit ) {
                streamlog_out( DEBUG1 ) << *itHit << endl;
                streamlog_out( DEBUG1 ) << setw( 10 ) << ( *itHit )->id( ) << endl;
                streamlog_out( DEBUG1 ) << setw( 10 ) << ( *itHit )->getPosition( )[0]
                        << setw( 10 ) << ( *itHit )->getPosition( )[1]
                        << setw( 10 ) << ( *itHit )->getPosition( )[2] << endl;
            }
        }

        _trackFinder->SetAllHits( allHitsArray );
        streamlog_out( DEBUG1 ) << "Trying to find tracks..." << endl;
        EUTelTrackFinder::SearchResult searchResult = _trackFinder->SearchTracks( );
        streamlog_out( DEBUG1 ) << "Search results retrieved..." << endl;
        streamlog_out( DEBUG1 ) << "Search results = " << ( int ) searchResult << endl;
        streamlog_out( DEBUG1 ) << "Retrieving track candidates..." << endl;
        trackCandidates = _trackFinder->GetTrackCandidates( );
        vector< vector<int> > indexarray;

        _nTracks = ( int ) trackCandidates.size( );
        static_cast < AIDA::IHistogram1D* > ( _aidaHistoMap1D[ _numberTracksCandidatesHistName ] ) -> fill( _nTracks );
        //        
        streamlog_out( DEBUG1 ) << "Event #" << _nProcessedEvents << endl;
        streamlog_out( DEBUG1 ) << "Track finder " << _trackFinder->GetName( ) << " found  " << _nTracks << endl;

        _trackFinder->Reset( ); // Breaks Fitter track candidates reference???!!!




//        // Write output collection
//
//        // add new track candidates to the collection
//        LCCollectionVec * trkCandCollection = 0;
//        try {
//            trkCandCollection = new LCCollectionVec( LCIO::LCGENERICOBJECT );
//        } catch ( ... ) {
//            streamlog_out  ( WARNING2 ) << "Can't allocate output collection" << endl;
//        }
//        
//        trkCandCollection->push_back( trackCandidates );
//        try {
//            evt->getCollection( _trackCandidateHitsOutputCollectionName ) ;
//        } catch ( ... ) {
//            evt->addCollection( trkCandCollection, _trackCandidateHitsOutputCollectionName );
//        }

    }

    _nProcessedEvents++;
}

// @TODO LCEvent* evt must be removed from the list of arguments

void EUTelProcessorTrackingExhaustiveTrackSearch::FillHits( LCEvent * evt,
                                                            LCCollection* collection,
                                                            vector< EVENT::TrackerHitVec >& allHitsArray ) const {

    int layerIndex = -1;

    // loop over all hits in collection
    for ( int iHit = 0; iHit < collection->getNumberOfElements( ); iHit++ ) {

        TrackerHitImpl * hit = static_cast < TrackerHitImpl* > ( collection->getElementAt( iHit ) );
        ///code hoes here;
        if ( Utility::HitContainsHotPixels( hit, _hotPixelMap ) ) {
            streamlog_out( DEBUG3 ) << "Hit " << iHit << " contains hot pixels; skip this one. " << std::endl;
            continue;
        }

        LCObjectVec clusterVector = hit->getRawHits( );

        EUTelVirtualCluster * cluster;
        cluster = Utility::GetClusterFromHit( hit );

        //Warning! It is very bad idea to do like this. This code must ideally be implemented in GetClusterFromHit
        if ( hit->getType( ) == kEUTelSparseClusterImpl ) {
            // ok the cluster is of sparse type, but we also need to know
            // the kind of pixel description used. This information is
            // stored in the corresponding original data collection.
            LCCollectionVec * sparseClusterCollectionVec = dynamic_cast < LCCollectionVec * > ( evt->getCollection( "original_zsdata" ) );

            TrackerDataImpl * oneCluster = dynamic_cast < TrackerDataImpl* > ( sparseClusterCollectionVec->getElementAt( 0 ) );
            CellIDDecoder<TrackerDataImpl > anotherDecoder( sparseClusterCollectionVec );
            SparsePixelType pixelType = static_cast < SparsePixelType > ( static_cast < int > ( anotherDecoder( oneCluster )["sparsePixelType"] ) );

            // now we know the pixel type. So we can properly create a new
            // instance of the sparse cluster
            if ( pixelType == kEUTelSimpleSparsePixel ) {
                cluster = new EUTelSparseClusterImpl< EUTelSimpleSparsePixel > ( static_cast < TrackerDataImpl * > ( clusterVector[ 0 ] ) );
            } else if ( pixelType == kEUTelAPIXSparsePixel ) {
                cluster = new EUTelSparseClusterImpl<EUTelAPIXSparsePixel > ( static_cast < TrackerDataImpl * > ( clusterVector[ 0 ] ) );
            } else {
                streamlog_out( ERROR4 ) << "Unknown pixel type.  Sorry for quitting." << std::endl;
                throw UnknownDataTypeException( "Pixel type unknown" );
            }
        }

        // @TODO add support of kEUTelAPIXClusterImpl type
        if ( hit->getType( ) == kEUTelAPIXClusterImpl ) {
            streamlog_out( MESSAGE4 ) << "kEUTelAPIXClusterImpl is not supported at the moment" << std::endl;
            continue;
            //                
            //            TrackerDataImpl * clusterFrame = static_cast<TrackerDataImpl*> (clusterVector[0]);
            //            EUTelSparseClusterImpl< EUTelAPIXSparsePixel > *apixCluster = new EUTelSparseClusterImpl< EUTelAPIXSparsePixel > (clusterFrame);
            //            int sensorID = apixCluster->getDetectorID();
            //            bool skipHit = false;
            //            for (size_t iPixel = 0; iPixel < apixCluster->size(); ++iPixel) {
            //                EUTelAPIXSparsePixel apixPixel;
            //                apixCluster->getSparsePixelAt(iPixel, &apixPixel);
            //                int pixelX = apixPixel.getXCoord();
            //                int pixelY = apixPixel.getYCoord();
            //                skipHit = !_rect.isInside(sensorID, pixelX, pixelY);
            //            }
            //
            //            if (skipHit) {
            //                streamlog_out(MESSAGE4) << "Skipping cluster due to rectangular cut!" << std::endl;
            //                continue;
            //            }
            //            if (skipHit) {
            //                streamlog_out(MESSAGE4) << "TEST: This message should never appear!" << std::endl;
            //            }
        }

        if ( hit->getType( ) == kEUTelDFFClusterImpl || hit->getType( ) == kEUTelFFClusterImpl ||
                hit->getType( ) == kEUTelSparseClusterImpl ) {
            if ( cluster->getTotalCharge( ) <= _mimosa26ClusterChargeMin ) {
                streamlog_out( DEBUG ) << " Thin cluster (charge <=" << _mimosa26ClusterChargeMin
                        << ") found and removed (hit type " << hit->getType( )
                        << " on detector w/ id " << cluster->getDetectorID( ) << ")" << std::endl;
                delete cluster;
                continue;
            }
        }

        unsigned int localSensorID = Utility::GuessSensorID( hit );
        layerIndex = _geometry->_sensorIDVecMap[ localSensorID ];
        allHitsArray[layerIndex].push_back( hit );

        delete cluster; // <--- destroying the cluster
    } // end loop over all hits in collection

}

void EUTelProcessorTrackingExhaustiveTrackSearch::check( LCEvent * evt ) {
    // nothing to check here
}

void EUTelProcessorTrackingExhaustiveTrackSearch::end( ) {

    delete _geometry;

    streamlog_out( MESSAGE ) << "EUTelProcessorTrackingExhaustiveTrackSearch::end()  " << name( )
            << " processed " << _nProcessedEvents << " events in " << _nProcessedRuns << " runs "
            << std::endl;

}

void EUTelProcessorTrackingExhaustiveTrackSearch::bookHistograms( ) {
#if defined(USE_AIDA) || defined(MARLIN_USE_AIDA)

    try {
        streamlog_out( DEBUG ) << "Booking histograms..." << std::endl;

        const int tracksNBin = 20;
        const double tracksMin = -0.5;
        const double tracksMax = 19.5;

        // Track candidate search
        AIDA::IHistogram1D * numberTracksCandidates =
                marlin::AIDAProcessor::histogramFactory( this )->createHistogram1D( _numberTracksCandidatesHistName, tracksNBin,
                tracksMin, tracksMax );

        if ( numberTracksCandidates ) {
            numberTracksCandidates->setTitle( "Number of track candidates;N tracks;N events" );
            _aidaHistoMap1D.insert( make_pair( _numberTracksCandidatesHistName, numberTracksCandidates ) );
        } else {
            streamlog_out( ERROR2 ) << "Problem booking the " << ( _numberTracksCandidatesHistName ) << endl;
            streamlog_out( ERROR2 ) << "Very likely a problem with path name. Switching off histogramming and continue w/o" << endl;
        }
    } catch ( lcio::Exception& e ) {
        streamlog_out( WARNING2 ) << "Can't allocate histgrams. Continue without histogramming" << endl;
    }
#endif // defined(USE_AIDA) || defined(MARLIN_USE_AIDA)
}

int EUTelProcessorTrackingExhaustiveTrackSearch::getMaxTrackCandidates( ) const {
    return _maxNTracks;
}

int EUTelProcessorTrackingExhaustiveTrackSearch::getAllowedMissingHits( ) const {
    return _maxMissingHitsPerTrackCand;
}

int EUTelProcessorTrackingExhaustiveTrackSearch::getFinderMode( ) const {
    return _finderMode;
}