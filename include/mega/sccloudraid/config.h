#ifndef MEGA_SCCR_CONFIG_H
#define MEGA_SCCR_CONFIG_H 1

#include <atomic>
#include <vector>
#include <string>

extern const char* std_config_file;
extern const char* std_local_config_file;

namespace mega::SCCR {

struct IPv6
{
    in6_addr ip;
    
    bool operator==(const IPv6 &) const;
    bool operator<(const IPv6 &) const;
    void tostring(char*, socklen_t) const;

    IPv6(const char*);
    IPv6(in6_addr*);
};

typedef std::multimap<std::string, IPv6*> stringip_map;
typedef std::multimap<IPv6, std::string> ipstring_map;
typedef std::map<std::string, std::string> settings_map;

//extern const char* std_config_file;
//extern const char* std_local_config_file;

class Config
{
    mtime_t lastmtime;
    std::atomic<time_t> lastcheck;
    
    std::atomic<stringip_map*> nameips;
    std::atomic<ipstring_map*> ipnames;
    std::atomic<settings_map*> settings;

public:
    static char* skipspace(char*);
    static char* findspace(char*);
    static char* findlastspace(char*);
    static std::vector<std::string> split(const std::string& line);

private:
    void update(bool printsettings = false);

    const char* filename;

    Config* parent = nullptr;

public:
    int ipsbyprefix(const unsigned char*, int, in6_addr*, int);
    int getallips(const char*, in6_addr*, int);
    int checkipname(in6_addr*, const char*);
    int getipname(in6_addr*, char*, int);

    // get a string or unsigned value. If not present in this instance, the parent (if any) is checked
    std::string getsetting_s(const std::string& key, const std::string& defaultvalue);
    size_t getsetting_u(const std::string& key, size_t defaultvalue);

    static void loadStandardFiles(const char* cdaemonname);
    static std::string daemonname;
    
    Config(const char* filename = nullptr, Config* parent = nullptr);
};

} // namespace

extern mega::SCCR::Config config, configLocal;

#endif
