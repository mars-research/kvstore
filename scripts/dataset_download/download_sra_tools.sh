wget https://ftp-trace.ncbi.nlm.nih.gov/sra/sdk/current/sratoolkit.current-ubuntu64.tar.gz
mkdir sratoolkit
tar xvf sratoolkit.current-ubuntu64.tar.gz -C sratoolkit --strip-components=1
rm sratoolkit.current-ubuntu64.tar.gz 
sratoolkit/bin/vdb-config -i