# Finding the Server IP Address

## Finding candidate IPs

Although I initially started with the entire public internet, I realised that with the network capacity I have access to, I need to reduce the search space if I want to complete the search in a reasonable time. So, I limited my search to the AS24940 Hetzner ASN. Using `masscan`, I got a list of 1728 hosts that respond to port 25565.

```bash
sudo masscan -p25565 --rate 3000 --source-port 60001 --wait 40 -oL scan.txt 116.202.0.0/16 116.203.0.0/16 128.140.0.0/17 135.181.0.0/16 136.243.0.0/16 138.201.0.0/16 142.132.128.0/17 144.76.0.0/16 148.251.0.0/16 157.90.0.0/16 159.69.0.0/16 162.55.0.0/16 167.233.0.0/16 167.235.0.0/16 168.119.0.0/16 171.25.225.0/24 176.9.0.0/16 178.212.75.0/24 178.63.0.0/16 185.107.52.0/22 185.110.95.0/24 185.112.180.0/24 185.126.28.0/22 185.12.65.0/24 185.136.140.0/23 185.157.176.0/23 185.157.178.0/23 185.157.83.0/24 185.171.224.0/22 185.189.228.0/24 185.189.229.0/24 185.189.230.0/24 185.189.231.0/24 185.209.124.0/22 185.213.45.0/24 185.216.237.0/24 185.226.99.0/24 185.228.8.0/23 185.242.76.0/24 185.36.144.0/22 185.50.120.0/23 188.34.128.0/17 188.40.0.0/16 193.110.6.0/23 193.163.198.0/24 193.25.170.0/23 194.35.12.0/23 194.42.180.0/22 194.42.184.0/22 194.62.106.0/24 195.201.0.0/16 195.248.224.0/24 195.60.226.0/24 195.96.156.0/24 197.242.84.0/22 201.131.3.0/24 213.133.96.0/19 213.232.193.0/24 213.239.192.0/18 23.88.0.0/17 45.136.70.0/23 45.148.28.0/22 45.15.120.0/22 46.4.0.0/16 49.12.0.0/16 49.13.0.0/16 5.75.128.0/17 5.9.0.0/16 65.108.0.0/16 65.109.0.0/16 65.21.0.0/16 78.46.0.0/15 83.219.100.0/22 83.243.120.0/22 85.10.192.0/18 88.198.0.0/16 88.99.0.0/16 91.107.128.0/17 91.190.240.0/21 91.233.8.0/22 94.130.0.0/16 94.154.121.0/24 95.216.0.0/16 95.217.0.0/16
```

## Getting server details

Next, I needed to see which one of those hosts are actually Minecraft servers, and gather information about the one that are actually Minecraft servers.

To do this, I implemented the Minecraft Server List Ping protocol  in C++ and built [a wrapper around it](src/minecraftscan.cpp) that scans a list of given IP addresses and dumps the SLP responses as JSON to a given file.

```C++
// Loop over all the hosts we need to scan
#pragma omp parallel for
for(int i = 0; i < hosts.size(); i++){
    auto host = hosts[i];

    int sockfd;

    std::string json;

    search(sockfd, host, json);

    // Close the socket if search() didn't already
    if(sockfd != -1){
        close(sockfd);
    }

    #pragma omp critical
    {
        if(json.size() != 0){
            // Write the SLP response to the provided file
            std::ofstream file;
            file.open(argv[2], std::ios::app);
            file << "{\"ip:\": \"" << host << "\", \"slp\": " << json << "}," << std::endl;
            file.close();
        }
        //std::cout << "Wrote one" << std::endl;
        incrementHostsScanned();
        updateMessage();
    }

}// Keep looping, go to the next host in the vector
```

## Finding the correct server

Now that I had a list of servers and their SLP responses, I needed to find the correct server out of the list. First, I tried doing a text search for "LiveOverflow", which is what I expected the MotD to include, however this was not the case. Then, I looked for the N00b bots in the player list, but couldn't find any. As a final resort, I filtered down all the servers to the correct version (Paper 1.19.2) and wrote a [Python script](src/get-favicons.py) to parse the favicon data from the SLP responses and write all of them to image files.

```python
#!/usr/bin/env python3

import base64

f = open("favicons-b64.txt")

favicons = []

for line in f:
    favicons.append(line)

line = 1

for favicon in favicons:
    outfile = open("favicons/" + str(line) + ".png", "wb")
    outfile.write(base64.b64decode(favicon))
    outfile.close()
    line += 1

```



After some careful scrolling and looking up the correct favicon data in the SLP responses, I found the IP address to be:

<details><summary>Spoiler</summary>65.109.68.176</details>
