

#include "insti.h"

//require c++11
static_assert(__cplusplus > 199711L, "Program requires C++11 capable compiler");

using namespace std;

/*
  #define DEBUG
  #define DEBUG2
  #define DEBUG3
*/

#ifdef DEBUG
#define DEBUG_MSG(str) do { std::cerr << str; } while( false )
#else
#define DEBUG_MSG(str) do { } while ( false )
#endif

#ifdef DEBUG2
#define DEBUG_MSG2(str) do { std::cerr << str; } while( false )
#else
#define DEBUG_MSG2(str) do { } while ( false )
#endif

#ifdef DEBUG3
#define DEBUG_MSG3(str) do { std::cerr << str; } while( false )
#else
#define DEBUG_MSG3(str) do { } while ( false )
#endif

//initializyng static member variables
int Insti::s_iEstimator;
uint Insti::s_uParallelChains;
uint Insti::s_uCycles;
bool Insti::s_bIsLogging = false;
bool Insti::s_bKickStartFromRef = false;
string Insti::s_sLegendFile = "";
string Insti::s_sRefHapsFile = "";

// return the probability of the model given the input haplotypes P and
// emission and transition matrices of individual I
// call Impute::hmm_like and print out result
fast    Insti::hmm_like(uint I, uint* P) {

    return Impute::hmm_like( I, P );
}

void Insti::SetLog( const string & sLogFile )
{
    s_bIsLogging = true;
    m_sLogFile = sLogFile;
    m_bLogIsGz = false;

    size_t found = sLogFile.find(".gz", sLogFile.length() - 3);
    if( found!=std::string::npos ){
        m_bLogIsGz = 1;
        m_gzLogFileStream = gzopen(m_sLogFile.c_str(), "wt");
    }
    else{

        // open logFileStream for append if it's not yet open
        if( !m_ofsLogFileStream ){
            m_ofsLogFileStream.open(m_sLogFile);
        }
        //otherwise close and open again
        else
        {
            m_ofsLogFileStream.close();
            m_ofsLogFileStream.open(m_sLogFile);
        }

        // exit if the file cannot be opened
        if( !m_ofsLogFileStream.is_open() ){
            cerr << "could not open log file "<< m_sLogFile <<" for writing" << endl;
            exit(1);
        }
    }

    cerr << "Logging to:\t" << m_sLogFile << endl;

};

void Insti::WriteToLog( const string & tInput )
{

    if(! s_bIsLogging)
        return;

    if(m_bLogIsGz){
        gzprintf(m_gzLogFileStream, tInput.c_str());
    }
    else{
        // open logFileStream for append if it's not yet open
        if( !m_ofsLogFileStream ){
            m_ofsLogFileStream.open(m_sLogFile);
        }

        // exit if the file cannot be opened
        if( !m_ofsLogFileStream.is_open() ){
            cerr << "could not open log file "<< m_sLogFile <<" for writing" << endl;
            exit(1);
        }

        // write input to log file
        m_ofsLogFileStream << tInput;
        m_ofsLogFileStream.flush();
    }
    DEBUG_MSG( "wrote something" <<endl);

};

// what to log when given an EMCChain
void Insti::WriteToLog( const EMCChain& rcChain, const bool bMutate){

    stringstream message;
    message << m_nIteration << "\t" << rcChain.m_uI << "\t" <<  rcChain.getLike() << "\t"
            << rcChain.m_uChainID << "\t" << rcChain.m_fTemp << "\t"
            << bMutate << endl;
    WriteToLog( message.str() );
}

// Roulette Wheel Selection, returns index of chain selected
int Insti::RWSelection( const vector <EMCChain> &rvcChains){

    double dTotalProb = 0; // always positive, could  be larger than 1
    for( const auto& icChain: rvcChains){
        dTotalProb += icChain.getSelection();
        DEBUG_MSG2("\ttotal prob:\t" << dTotalProb <<endl);
    }

    assert(dTotalProb > 0);
    assert(dTotalProb < std::numeric_limits<double>::max());
    double dStopPoint = gsl_rng_uniform(rng) * dTotalProb;
    assert( dStopPoint > 0 );

    DEBUG_MSG2( "\t..." << endl);
    DEBUG_MSG2( "\ttotalProb:\t" << dTotalProb << endl );
    int iChainIndex = 0;
    do
        dStopPoint -= rvcChains[iChainIndex].getSelection();
    while( dStopPoint > 0 && ++iChainIndex);
    assert(iChainIndex >= 0);
    assert(iChainIndex < static_cast<int>( rvcChains.size() ) );

    return iChainIndex;
}


bool    Insti::load_bin(const char *F) {

    Impute::load_bin(F);
    
    // setting number of cycles to use
    // here is best place to do it because in is defined in load_bin()

    if( s_uCycles > 0){
        m_uCycles = s_uCycles;
    }
    else{
        m_uCycles = nn * in;  // this was how snptools does it
    }

    return true;
}


// this is where I load the legend and haps files
bool Insti::load_refPanel(string legendFile, string hapsFile){

//    cerr << "Loading Reference Panel..." << endl;

    // make sure both files are defined
    if(legendFile.size() == 0){
        cerr << "Need to define a legend file if defining a reference haplotypes file";
        exit(1);
    }
    if(hapsFile.size() == 0){
        cerr << "Need to define a reference haplotypes file if defining a legend file";
        exit(1);
    }

    m_bUsingRefHaps = true;

    // read in legend file
    ifile legendFD(legendFile);
    string buffer;

    // discard header
    unsigned uLineNum = 0;
    while(getline(legendFD, buffer, '\n')){
        uLineNum++;
        vector<string> tokens;
        sutils::tokenize(buffer, tokens);

        // make sure header start is correct
        if(uLineNum == 1){
            vector<string> headerTokenized;
            string header = "id position a0 a1";
            sutils::tokenize(header, headerTokenized);
            for(int i = 0; i != 4; i++){
                try{
                    if(tokens[i] != headerTokenized[i])
                        throw myException("Error in legend file ("+ legendFile +"): header start does not match:\n\t" + header + "\n.  Instead the first line of the header is:\n\t" + buffer + "\n");
                }
                catch (exception& e){
                    cout << e.what() << endl;
                    exit(2);
                }
            }
            continue;
        }

        // now make sure legend file matches sites
        try{
            if(tokens[1] != sutils::uint2str(site[uLineNum - 2].pos) )
                throw myException("Error at Line " + sutils::uint2str(uLineNum) + " in legend file:\tPosition " + tokens[1] + " in legend file needs to match position in probin file: " + sutils::uint2str(site[uLineNum - 2].pos));
        }
        catch (exception& e){
            cout << e.what() << endl;
            exit(1);
        }
        assert(tokens[2]+tokens[3] == site[uLineNum - 2].all && "Alleles in legend file need to match current data");

    }

    assert(site.size() == uLineNum -1 && "Number of Positions in legend file needs to match current data");

    // given that the legend looks good, read in the haps file
    ifile hapsFD(hapsFile);
    uLineNum = 0;

    while(getline(hapsFD, buffer, '\n')){
        uLineNum++;
        vector<string> tokens;
        sutils::tokenize(buffer, tokens);

        // make sure header start is correct
        if(uLineNum == 1){

            // count number of haps
            m_uNumRefHaps = tokens.size();

            // resize the vector for holding ref haps
            m_vRefHaps.resize(m_uNumRefHaps * wn);

        }

        assert(tokens.size() == m_uNumRefHaps && "Every row of haplotypes file must have the same number of columns");

        // store ref haplotypes
        for( unsigned i = 0; i != tokens.size(); i++){

            int val = atoi(tokens[i].c_str());
            if(val == 0){
                set0(&m_vRefHaps[i * wn], uLineNum -1);
            }
            else if(val == 1){
                set1(&m_vRefHaps[i * wn], uLineNum -1);
            }
            else{
                cerr << "All alleles are not 0 or 1 In haplotypes file "<< hapsFile <<" at line number " << uLineNum << endl;
                throw 1;
            }

        }
    }

    if(m_uNumRefHaps == 0){
        cerr << "num ref haps is 0.  Haps file empty?";
        throw 2;
    }

    cerr << "Reference panel haplotypes\t" << m_uNumRefHaps << endl;

    return true;

}

/* CHANGES from impute.cpp
   support for reference haplotypes
*/

void Insti::initialize(){

    Impute::initialize();
        
    // load ref haps
    if(s_sLegendFile.size() > 0 || s_sRefHapsFile.size() > 0){
        load_refPanel( s_sLegendFile, s_sRefHapsFile);
    }

    if(m_bUsingRefHaps){
        // add ref haplotypes to sample haps
        haps.insert(haps.end(), m_vRefHaps.begin(), m_vRefHaps.end());

        // enlarge hnew so haps and hnew can be swapped
        // the ref haps are never updated, so they'll stick around forever
        hnew.insert(hnew.end(), m_vRefHaps.begin(), m_vRefHaps.end());

        // re-assign pare in light of the haplotypes
//        pare.assign(in * (in + m_uNumRefHaps/2), 0);
    }

}

// this part of the code seems to be responsible for:
// A - finding a set of four haps that are close to the current individual
// B - running the HMM and udating the individual I's haplotypes
// A takes much longer than B

/* CHANGES from impute.cpp:
   moved logging to solve from hmm_like()
   cycles is now stored in a private member variable and defined after load_bin() time
*/

// solve(individual, number of cycles, penalty, burnin?)
fast Insti::solve(uint I, uint    &N, fast pen, RelationshipGraph &oRelGraph) {

    // write log header
    stringstream message;
    message << "##iteration\tindividual\tproposal" << endl;
    WriteToLog( message.str() );

    // pick 4 haplotype indices at random not from individual
    uint p[4];
    for (uint j = 0; j < 4; j++) {
        p[j] = oRelGraph.SampleHap(I, rng);
    }

    // get a probability of the model for individual I given p
    fast curr = hmm_like(I, p);

    // pick a random haplotype to replace with another one from all
    // haplotypes.  calculate the new probability of the model given
    // those haplotypes.
    // accept new set if probability has increased.
    // otherwise, accept with penalized probability
    for (uint n = 0; n < N; n++) {  // fixed number of iterations
        
        uint rp = gsl_rng_get(rng) & 3, oh = p[rp];

        // kickstart phasing and imputation by only sampling haps
        // from ref panel in first round
        if(s_bKickStartFromRef && n == 0){
            p[rp] = oRelGraph.SampleHap(I, rng, true);
        }
        else{
            p[rp] = oRelGraph.SampleHap(I, rng);
        }
        
        fast prop = hmm_like(I, p);
        bool bAccepted = false;
        if (prop > curr || gsl_rng_uniform(rng) < exp((prop - curr) * pen)) {
            curr = prop;
            bAccepted = true;
        }
        else p[rp] = oh;

        // Update relationship graph with proportion pen
        oRelGraph.UpdateGraph(p, bAccepted, I, pen);
                
        // log accepted proposals
        if(bAccepted){
            stringstream message;
            message << m_nIteration << "\t" << I << "\t" <<  prop << endl;
            WriteToLog( message.str() );
        }
    }

    // if we have passed the burnin cycles (n >= bn)
    // start sampling the haplotypes for output
    /*
    if (P) {
        uint16_t *pa = &pare[I * in];
        for (uint i = 0; i < 4; i++) pa[p[i] / 2]++;
    }
    */
    hmm_work(I, p, pen);
    return curr;
}

/* CHANGES from impute.cpp
   added a member variable for n so
*/

void    Insti::estimate() {
    cerr.setf(ios::fixed);
    cerr.precision(3);
    cerr << "iter\tpress\tlike\tfold\trunTime\texpectedRunTime" << endl;

    m_oRelGraph.init(2, in, hn + m_uNumRefHaps);
    
    // n is number of cycles = burnin + sampling cycles
    // increase penalty from 2/bn to 1 as we go through burnin
    // iterations.
    for (uint n = 0; n < bn + sn; n++) {
        m_nIteration = n;
        fast sum = 0, pen = min<fast>(2 * (n + 1.0f) / bn, 1), iter = 0;
        pen *= pen;  // pen = 1 after bn/2 iterations
        for (uint i = 0; i < in; i++) {
            sum += solve(i, m_uCycles, pen, m_oRelGraph);  // call solve=> inputs the sample number,
            iter += m_uCycles;
        }
        swap(hnew, haps);
        if (n >= bn) for (uint i = 0; i < in; i++) replace(i);  // call replace
        cerr << n << '\t' << pen << '\t' << sum / in / mn << '\t' << iter / in / in << endl;
    }
    cerr << endl;
    result();    // call result

}

/* estimate_EMC -- Evolutionary Monte Carlo
   Here we try to increase the speed of convergence by
   running a parallel chain evolutionary monte carlo scheme.

   The reference for this implementation is
   "Advanced Markov Choin Monte Carlo Methods" by Liang, Liu and Carroll
   first edition?, 2010, pp. 128-132
*/

// solve(individual, number of cycles, penalty, burnin?)
fast Insti::solve_EMC(uint I, uint  N, fast S) {

    DEBUG_MSG( "Entering solve_EMC..." << endl);
    // for lack of a better place, define free parameters here
    fast fMutationRate = 0.3f; // see p.134 of Liang et al.
    fast fSelectTemp = 10000;
    fast fMaxTemp = Insti::s_uParallelChains;

    // write log header
    stringstream message;
    message << "##iteration\tindividual\tproposal\tchainID\tchainTemp"
            << "\tmutation" << endl;
    WriteToLog( message.str() );

    // initialize emc chains with increasing temperatures
    vector <EMCChain> vcChains;
    vector <uint> vuChainTempHierarchy; // index of Chains sorted by temperature, ascending
    for (uint i = 0; i < Insti::s_uParallelChains; i++){
        vcChains.push_back( EMCChain( (i+1) * fMaxTemp / Insti::s_uParallelChains, fSelectTemp, I, in, i) );

        DEBUG_MSG2( "\tlength of vcChains\t" << vcChains.size() << endl);
        // initialize current likelihood

        // randomize parent haps
        for (uint j = 0; j < 4; j++) {
            do {
                uint uChosenHap = gsl_rng_get(rng) % vcChains[i].m_uHapNum;
                vcChains[i].setParent(j, uChosenHap );
                DEBUG_MSG3("\t\tchosen hap:\t" << uChosenHap << endl << "\t\tchosen parent:\t" << vcChains[i].getParent(j) << endl);

            }
            while (vcChains[i].getParent(j) / 2 == vcChains[i].m_uI);
        }

        DEBUG_MSG2("\tsetting likelihood"<<endl;);
        // set likelihood
        vcChains[i].setLike( hmm_like(vcChains[i].m_uI, vcChains[i].getParents() ));
        vuChainTempHierarchy.push_back(i);
    }

    // pick a random haplotype to replace with another one from all
    // haplotypes.  calculate the new probability of the model given
    // those haplotypes.
    // accept new set if probability has increased.
    // otherwise, accept with penalized probability
    for (uint n = 0; n < N; n++) {  // fixed number of iterations

        DEBUG_MSG2( "\tIteration " << n << endl);
        //  now choose whether to mutate or crossover
        bool bMutate =  gsl_rng_uniform(rng) > fMutationRate;
        fast prop;
        if(bMutate){
            // mutate

            DEBUG_MSG2("\tMutating...");
            // choose chain randomly (uniform)
            uint j = gsl_rng_get(rng) % Insti::s_uParallelChains;

            DEBUG_MSG2( "\t" << "Temp: " << vcChains[j].m_fTemp);
            fast curr = vcChains[j].getLike();

            // choose parent hap (rp) to mutate
            // replaced hap is stored in oh (Original Hap)
            uint rp = gsl_rng_get(rng) & 3, oh = vcChains[j].getParent(rp);

            // mutate parent hap
            do vcChains[j].setParent(rp,gsl_rng_get(rng) % hn); while (vcChains[j].getParent(rp) / 2 == I);

            // calculate acceptance probability
            prop = hmm_like(vcChains[j].m_uI, vcChains[j].getParents());
            if (prop > curr || gsl_rng_uniform(rng) < exp( ( curr - prop ) / vcChains[j].m_fTemp)) {
                vcChains[j].setLike(prop);

                WriteToLog( vcChains[j], bMutate );
            }
            else vcChains[j].setParent(rp, oh);

        }
        else{
            //crossover

            DEBUG_MSG2( "\tCrossing Over...");
            // 1. choose random chain to work on by roulette wheel selection
            int iFirstChainIndex = RWSelection(vcChains);

            DEBUG_MSG2( "\t\tFirst Chain:\t" << vcChains[iFirstChainIndex].m_uChainID << endl);
            DEBUG_MSG2( "\t\tSelecting second chain:");
            // 2. select second chain at random (uniform) from remaining chains
            int iSecondChain;
            do {
                iSecondChain = gsl_rng_get(rng) % Insti::s_uParallelChains;
            }
            while (iSecondChain == iFirstChainIndex);

            //---- only crossover with certain probability depending
            //---- on crossover probabilities of parents
            // A. save parents and likelihoods of original chains

            EMCChain cFirstOrigChain = vcChains[iFirstChainIndex];
            EMCChain cSecondOrigChain = vcChains[iSecondChain];
            bool bFirstOrigChainHigherLike = cFirstOrigChain.getLike() > cSecondOrigChain.getLike();

            // B. cross over
            // uniform crossover: find haps to keep
            word cSelection = static_cast<word>( gsl_rng_get(rng) & 15);
            for(uint i = 0; i < 4; i++){

                // if bit at location i is 1, exchange haps
                if( test( &cSelection, i) ) {
                    uint oh = vcChains[iFirstChainIndex].getParent(i);
                    vcChains[iFirstChainIndex].setParent(i, vcChains[iSecondChain].getParent(i));
                    vcChains[iSecondChain].setParent(i, oh);
                }
            }

            // update likelihoods of crossed over chains
            auto& rcFirstChain = vcChains[iFirstChainIndex];
            rcFirstChain.setLike( hmm_like(rcFirstChain.m_uI, rcFirstChain.getParents()));
            auto& rcSecondChain = vcChains[iSecondChain];
            rcSecondChain.setLike( hmm_like(rcSecondChain.m_uI, rcSecondChain.getParents()));
            bool const bFirstChainHigherLike = cFirstOrigChain.getLike() > cSecondOrigChain.getLike();


            // C. deterimen if cross-over was successful
            bool bCrossAccepted = false;
            // the order of likelihoods is not the same
            if( bFirstOrigChainHigherLike ^ bFirstChainHigherLike){
                bCrossAccepted = gsl_rng_uniform(rng)
                    <= exp((( cSecondOrigChain.getLike() - rcFirstChain.getLike() ) / cSecondOrigChain.m_fTemp )
                            + ( cFirstOrigChain.getLike() - rcSecondChain.getLike() ) / cFirstOrigChain.m_fTemp );
            }

            // the order of the likelihoods matches
            else{
                bCrossAccepted = gsl_rng_uniform(rng)
                    <= exp((( cSecondOrigChain.getLike() - rcSecondChain.getLike() ) / cSecondOrigChain.m_fTemp )
                            + ( cFirstOrigChain.getLike() - rcFirstChain.getLike() ) / cFirstOrigChain.m_fTemp );
            }

            // replace old chains if cross-over was not accepted
            if( !bCrossAccepted) {
                vcChains[iFirstChainIndex] = cFirstOrigChain;
                vcChains[iSecondChain] = cSecondOrigChain;

                stringstream message;
                message << "# Unsuccessful Crossover\tChainIDs:\t" << rcFirstChain.m_uChainID << "\t" << rcSecondChain.m_uChainID << endl;
                WriteToLog( message.str() );
            }
            else{

                // otherwise log changes to likelihood
                WriteToLog( vcChains[iFirstChainIndex], bMutate );
                WriteToLog( vcChains[iSecondChain], bMutate);
            }

        }

        // now try Insti::s_uParallelChains exchanges
        DEBUG_MSG2( "\tExchanging..."<<endl);
        uint uNumExchanges = 0;
        for( uint i = 0; i < Insti::s_uParallelChains; i++){

            uint uFirstChainIndex = gsl_rng_get(rng) % Insti::s_uParallelChains;
            DEBUG_MSG3( "\t\tfirstChainIndex " << uFirstChainIndex);

            uint uFirstChainHierarchyIndex = vuChainTempHierarchy[ uFirstChainIndex ];
            DEBUG_MSG3( "\tfirst chain: " << vuChainTempHierarchy[ uFirstChainIndex ]);

            // selecting second chain
            uint uSecondChainIndex;
            if (uFirstChainIndex == 0)
                uSecondChainIndex = uFirstChainIndex + 1;
            else if ( uFirstChainIndex == Insti::s_uParallelChains - 1)
                uSecondChainIndex = Insti::s_uParallelChains - 2;
            else if( gsl_rng_get(rng) & 1 )
                uSecondChainIndex = uFirstChainIndex - 1;
            else
                uSecondChainIndex = uFirstChainIndex + 1;

            uint uSecondCHI = vuChainTempHierarchy[ uSecondChainIndex ];

            DEBUG_MSG3( "\tsecond chain: " << vuChainTempHierarchy[ uSecondChainIndex ]);

            // MH step for exchange
            fast fAcceptProb = min<fast>( exp( (vcChains[uFirstChainHierarchyIndex].getLike() - vcChains[uSecondCHI].getLike())
                                            * ( (1/vcChains[uFirstChainHierarchyIndex].m_fTemp) - (1/vcChains[uSecondCHI].m_fTemp)))
                                      , 1);

            DEBUG_MSG3( "\taccept prob: " << fAcceptProb);
            // exchange with acceptance probability
            if( gsl_rng_uniform(rng) < fAcceptProb){

                // exchange temperatures
                fast fTemp = vcChains[uFirstChainHierarchyIndex].m_fTemp;
                vcChains[uFirstChainHierarchyIndex].setTemp(vcChains[uSecondCHI].m_fTemp);
                vcChains[uSecondCHI].setTemp(fTemp);

                // exchange location in vcChains
                std::swap(vuChainTempHierarchy[uFirstChainIndex], vuChainTempHierarchy[uSecondChainIndex]);
                ++ uNumExchanges;
            }
            DEBUG_MSG3( "\tnumExchanges: " << uNumExchanges << endl);

        }

        // keep track of number of exchanges
        stringstream message;
        message << "# Number of Exchanges out of total:\t" << uNumExchanges <<
            "\t" << Insti::s_uParallelChains << endl;
        WriteToLog( message.str() );
    }

    // now select a chain for sampling according to roulette wheel selection
    int iFirstChainIndex = RWSelection(vcChains);

    auto& rcFirstChain = vcChains[iFirstChainIndex];


    // if we have passed the burnin cycles (n >= bn)
    // start sampling the haplotypes for output
    /*
    if (P) {
        uint16_t *pa = &pare[I * in];
        for (uint i = 0; i < 4; i++) pa[rcFirstChain.getParent(i) / 2]++;
    }
    */

    DEBUG_MSG( "Updating individual " << I << "\n");
    // update haplotypes of I

//    cerr << "parents (2 lines):" << endl;
//    cerr << rcFirstChain.m_auParents[1] << endl;
//    cerr << vcChains[ iFirstChainIndex ].m_auParents[1] << endl;
    hmm_work(I, rcFirstChain.getParents(), S);
    return rcFirstChain.getLike();
}

/* estimate_EMC -- Evolutionary Monte Carlo
   Here we try to increase the speed of convergence by
   running a parallel chain evolutionary monte carlo scheme.

   The reference for this implementation is
   "Advanced Markov Choin Monte Carlo Methods" by Liang, Liu and Carroll
   first edition?, 2010, pp. 128-132
*/

void    Insti::estimate_EMC() {
    cerr.setf(ios::fixed);
    cerr.precision(3);
    cerr << "Running Evolutionary Monte Carlo\n";
    cerr << "iter\tpress\tlike\tfold\n";

    // n is number of cycles = burnin + sampling cycles
    // increase penalty from 2/bn to 1 as we go through burnin
    // iterations.
    for (uint n = 0; n < bn + sn; n++) {
        m_nIteration = n;
        fast sum = 0, pen = min<fast>(2 * (n + 1.0f) / bn, 1), iter = 0;
        pen *= pen;  // pen = 1 after bn/2 iterations
        for (uint i = 0; i < in; i++) {
            sum += solve_EMC(i, m_uCycles, pen);  // call solve=> inputs the sample number,
            iter += m_uCycles;
        }
        swap(hnew, haps);
        if (n >= bn) for (uint i = 0; i < in; i++) replace(i);  // call replace
        cerr << n << '\t' << pen << '\t' << sum / in / mn << '\t' << iter / in / in << '\r';
    }
    cerr << endl;
    result();    // call result
}


/* estimate_AMH -- Adaptive Metropolis Hastings
   Here we try to increase the speed of convergence by
   keeping track of which individuals tend to copy from each other

   The reference for parts of this implementation is
   "Advanced Markov Choin Monte Carlo Methods" by Liang, Liu and Carroll
   first edition?, 2010, pp. 309
*/
void    Insti::estimate_AMH(unsigned uRelMatType) {
    cerr.setf(ios::fixed);
    cerr.precision(3);
    cerr << "Running Adaptive Metropolis Hastings\n";
    cerr << "iter\tpress\tlike\tfold" << endl;

    // create a relationshipGraph object
    // initialize relationship matrix
    // create an in x uSamplingInds matrix
    m_oRelGraph.init(uRelMatType, in, hn + m_uNumRefHaps);

    // n is number of cycles = burnin + sampling cycles
    // increase penalty from 2/bn to 1 as we go through burnin
    // iterations.

    for (uint n = 0; n < bn + sn; n++) {
//        cerr << "iter\t" << n << endl;
        m_nIteration = n;
        fast sum = 0, pen = min<fast>(2 * (n + 1.0f) / bn, 1), iter = 0;
        pen *= pen;  // pen = 1 after bn/2 iterations

        // update all individuals once
        for (uint i = 0; i < in; i++) {
//            if( i % 1000 == 0)
//                cerr << "cycle\t" << i << endl;
            sum += solve(i, m_uCycles, pen, m_oRelGraph);
            iter += m_uCycles;
        }
        swap(hnew, haps);
        if (n >= bn) for (uint i = 0; i < in; i++) replace(i);  // call replace
        cerr << n << '\t' << pen << '\t' << sum / in / mn << '\t' << iter / in / in << endl;
    }
    cerr << endl;
    result();    // call result
}

void Insti::save_relationship_graph ( string sOutputFile ){
    vector<string> vsSampNames;
    vsSampNames.insert(vsSampNames.end(), name.begin(), name.end());
    for(uint i = 0; i < ceil(m_uNumRefHaps/2); i++)  vsSampNames.push_back(string("refSamp") + sutils::uint2str(i));
    m_oRelGraph.Save(sOutputFile, vsSampNames);
}

void    Insti::document(void) {
    cerr << "\nimpute";
    cerr << "\nhaplotype imputation by cFDSL distribution";
    cerr << "\nauthor   Yi Wang @ Fuli Yu' Group @ BCM-HGSC";
    cerr << "\nusage    impute [options] 1.bin 2.bin ...";
    cerr << "\n\t-d <density>    relative SNP density to Sanger sequencing (1)";
    cerr << "\n\t-b <burn>       burn-in generations (56)";
    cerr << "\n\t-l <file>       list of input files";
    cerr << "\n\t-m <mcmc>       sampling generations (200)";
    cerr << "\n\t-n <fold>       sample size*fold of nested MH sampler iteration (2)";
//    cerr << "\n\t-t <thread>     number of threads (0=MAX)";
    cerr << "\n\t-v <vcf>        integrate known genotype in VCF format";
    cerr << "\n\t-c <conf>       confidence of known genotype (0.9998)";
    cerr << "\n\t-x <gender>     impute x chromosome data";
    cerr << "\n\t-e <file>       write log to file";
    cerr << "\n\t-E <integer>    choice of estimation algorithm (0)";
    cerr << "\n\t                0 - Metropolis Hastings with simulated annealing";
    cerr << "\n\t                1 - Evolutionary Monte Carlo with -p parallel chains";
    cerr << "\n\t                2 - Adaptive Metropolis Hastings - sample/sample matrix";
    cerr << "\n\t                3 - Adaptive Metropolis Hastings - sample/haplotype matrix";
    cerr << "\n\t-p <integer>    number of parallel chains to use in parallel estimation algorithms";
    cerr << "\n\t                (at least 2, default 5)";
    cerr << "\n\t-C <integer>    number of cycles to estimate an individual's parents before updating";

    cerr << "\n\nREFERENCE PANEL OPTIONS";
    cerr << "\n\t-H <file>       Impute2 style haplotypes file";
    cerr << "\n\t-L <file>       Impute2 style legend file";
    cerr << "\n\t-C <integer>    number of cycles to estimate an individual's parents before updating";    
    cerr << "\n\t-k              Kickstart phasing by using only ref panel in first iteration";
    cerr << "\n\n";
    exit(1);
}