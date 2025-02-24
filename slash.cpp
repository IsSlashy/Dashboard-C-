#include <iostream>
#define _WINSOCKAPI_  
#include <windows.h>
#include <psapi.h>
#include <vector>
#include <tlhelp32.h>
#include <iomanip>
#include <chrono>
#include <thread>
#include <iphlpapi.h>  // Pour les connexions réseau
#include <ws2tcpip.h>  // Pour les conversions d'adresses IP
#include <nvapi.h>     // Pour NVAPI, gestion des températures NVIDIA
#include "openhardwaremonitor-master/#include "openhardwaremonitor-master/OpenHardwareMonitorApi.h""
#include <winsock2.h>   // Utiliser Winsock 2 uniquement
#include <windows.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <PDHMsg.h>
#include <pdh.h>
#include <mutex>
#include <memory>
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "nvapi64.lib")

using namespace std;
string ipToString(DWORD ip) {
    in_addr addr;
    addr.s_addr = ip;
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr, str, INET_ADDRSTRLEN);
    return string(str);
}
// Fonction pour convertir wstring en string
string wide_string_to_string(const wstring& wide_string) {
    if (wide_string.empty()) return string();
    
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wide_string.data(), static_cast<int>(wide_string.size()), nullptr, 0, nullptr, nullptr);
    
    string result(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide_string.data(), static_cast<int>(wide_string.size()), &result[0], size_needed, nullptr, nullptr);
    
    return result;
}
// Structure pour stocker les températures
struct SystemTemperatures {
    vector<pair<string, float>> cpuTemps;  // Nom du cœur et température
    vector<pair<string, float>> gpuTemps;  // Nom du GPU et température
    bool valid = false;
};
// Classe pour gérer les températures du système
class TemperatureMonitor {
private:
    IWbemLocator* pLoc = nullptr;
    IWbemServices* pSvc = nullptr;
    bool wmiInitialized = false;
    mutex dataMutex;
    SystemTemperatures currentTemps;
    
    bool initializeWMI() {
        HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
        if (FAILED(hr)) return false;

        hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, 
                            IID_IWbemLocator, (LPVOID*)&pLoc);
        if (FAILED(hr)) {
            CoUninitialize();
            return false;
        }

        hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, 0, 
                                0, 0, 0, &pSvc);
        if (FAILED(hr)) {
            pLoc->Release();
            CoUninitialize();
            return false;
        }

        wmiInitialized = true;
        return true;
    }

public:
    TemperatureMonitor() {
        if (!initializeWMI()) {
            cerr << "Erreur d'initialisation WMI" << endl;
        }
    }

    ~TemperatureMonitor() {
        if (pSvc) pSvc->Release();
        if (pLoc) pLoc->Release();
        CoUninitialize();
    }

    SystemTemperatures getTemperatures() {
        lock_guard<mutex> lock(dataMutex);
        currentTemps.cpuTemps.clear();
        currentTemps.gpuTemps.clear();

        // Obtenir températures CPU via WMI
        if (wmiInitialized) {
            IEnumWbemClassObject* pEnumerator = nullptr;
            HRESULT hr = pSvc->ExecQuery(bstr_t("WQL"),
                bstr_t("SELECT * FROM Win32_PerfFormattedData_Counters_ThermalZoneInformation"),
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                nullptr, &pEnumerator);

            if (SUCCEEDED(hr) && pEnumerator) {
                IWbemClassObject* pclsObj = nullptr;
                ULONG uReturn = 0;
                
                while (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) == S_OK) {
                    VARIANT vtProp, vtName;
                    pclsObj->Get(L"Temperature", 0, &vtProp, 0, 0);
                    pclsObj->Get(L"Name", 0, &vtName, 0, 0);
                    
                    if (SUCCEEDED(hr)) {
                        float temp = (vtProp.intVal - 273.15f);
                        string name = _bstr_t(vtName.bstrVal);
                        currentTemps.cpuTemps.push_back({name, temp});
                    }
                    
                    VariantClear(&vtProp);
                    VariantClear(&vtName);
                    pclsObj->Release();
                }
                pEnumerator->Release();
            }
        }
NvPhysicalGpuHandle nvGPUHandle[NVAPI_MAX_PHYSICAL_GPUS] = { 0 };
 NvU32 gpuCount = 0;

 if (NvAPI_Initialize() == NVAPI_OK) {
            if (NvAPI_EnumPhysicalGPUs(nvGPUHandle, &gpuCount) == NVAPI_OK) {
        for (NvU32 i = 0; i < gpuCount; i++) {
            NV_GPU_THERMAL_SETTINGS thermalSettings;
            thermalSettings.version = NV_GPU_THERMAL_SETTINGS_VER;
            
            if (NvAPI_GPU_GetThermalSettings(nvGPUHandle[i], 0, &thermalSettings) == NVAPI_OK) {
                char name[64];
                NvAPI_GPU_GetFullName(nvGPUHandle[i], name);
                currentTemps.gpuTemps.push_back({
                    string(name),
                    static_cast<float>(thermalSettings.sensor[0].currentTemp)
                });
            }
        }
    }
    NvAPI_Unload();
}

 currentTemps.valid = true;
        return currentTemps;
    }  // Fin de getTemperatures()
};  // Fin de la classe TemperatureMonitor

void drawTemperatureInfo(shared_ptr<TemperatureMonitor> monitor) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    
    while (true) {
        SystemTemperatures temps = monitor->getTemperatures();
        if (!temps.valid) {
            this_thread::sleep_for(chrono::seconds(1));
            continue;
        }

        GetConsoleScreenBufferInfo(hConsole, &csbi);
        COORD originalPos = csbi.dwCursorPosition;

        string display;
        for (const auto& cpu : temps.cpuTemps) {
            display += cpu.first + ": " + to_string(static_cast<int>(cpu.second)) + "°C | ";
        }
        for (const auto& gpu : temps.gpuTemps) {
            display += gpu.first + ": " + to_string(static_cast<int>(gpu.second)) + "°C | ";
        }

        SHORT posX = max(0, csbi.srWindow.Right - static_cast<int>(display.length()) - 1);
        COORD pos = {posX, 0};

        SetConsoleCursorPosition(hConsole, pos);
        
        bool highTemp = false;
        for (const auto& temp : temps.cpuTemps) {
            if (temp.second > 80.0f) highTemp = true;
        }
        for (const auto& temp : temps.gpuTemps) {
            if (temp.second > 80.0f) highTemp = true;
        }

        SetConsoleTextAttribute(hConsole, highTemp ? 
            (FOREGROUND_RED | FOREGROUND_INTENSITY) : 
            (FOREGROUND_GREEN | FOREGROUND_INTENSITY));

        cout << display;
        SetConsoleCursorPosition(hConsole, originalPos);
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);

        this_thread::sleep_for(chrono::seconds(1));
        
    }
}
struct ProcessInfo {
    string name;
    SIZE_T memoryUsage;
    DWORD pid;
    bool suspicious;
};

// Fonction pour afficher ou masquer le curseur de la console
void ShowConsoleCursor(bool showFlag) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(out, &cursorInfo);
    cursorInfo.bVisible = showFlag;
    SetConsoleCursorInfo(out, &cursorInfo);
}

// Fonction pour imprimer une barre de progression
void printProgressBar(int progress, int total, int barWidth = 70) {
    float ratio = static_cast<float>(progress) / total;
    int completedWidth = static_cast<int>(ratio * barWidth);

    cout << "\rProgression : [";
    for (int i = 0; i < barWidth; ++i) {
        if (i < completedWidth) cout << "█";
        else cout << " ";
    }
    cout << "] " << static_cast<int>(ratio * 100.0) << "%" << flush;
}

// Fonction pour scanner les processus en cours
vector<ProcessInfo> scanProcesses(bool showProgress = true) {
    vector<ProcessInfo> processes;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(pe32);

        // Premier scan pour compter le nombre total de processus
        int totalProcesses = 0;
        if (Process32FirstW(snapshot, &pe32)) {
            do {
                ++totalProcesses;
            } while (Process32NextW(snapshot, &pe32));
        }

        // Reset pour le vrai scan
        CloseHandle(snapshot);
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        pe32.dwSize = sizeof(pe32);
        int currentProcess = 0;

        if (Process32FirstW(snapshot, &pe32)) {
            do {
                if (showProgress) {
                    ++currentProcess;
                    printProgressBar(currentProcess, totalProcesses);
                }

                HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, 
                    FALSE, pe32.th32ProcessID);
                
                if (hProcess) {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                        ProcessInfo pi;
                        pi.name = wide_string_to_string(wstring(pe32.szExeFile));
                        pi.memoryUsage = pmc.WorkingSetSize;
                        pi.pid = pe32.th32ProcessID;
                        
                        static const vector<string> suspicious = {
                            "miner", "crypto", "bitcoin", "malware",
                            "trojan", "backdoor", "keylogger", "svchost",
                            "hidden", "stealth", "inject"
                        };
                        
                        pi.suspicious = any_of(suspicious.begin(), suspicious.end(),
                            [&pi](const string& s) {
                                // Conversion en minuscules pour la comparaison
                                string nameLower = pi.name;
                                transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                                    [](unsigned char c) { return static_cast<char>(tolower(c)); });
                                string sLower = s;
                                transform(sLower.begin(), sLower.end(), sLower.begin(),
                                    [](unsigned char c) { return static_cast<char>(tolower(c)); });
                                return nameLower.find(sLower) != string::npos;
                            });
                        
                        processes.push_back(pi);
                    }
                    CloseHandle(hProcess);
                }
            } while (Process32NextW(snapshot, &pe32));
        }
        CloseHandle(snapshot);
    }
    
    if (showProgress) cout << endl;
    return processes;
}

// Fonction pour nettoyer la RAM
void cleanRAM() {
    MEMORYSTATUSEX memInfo = { sizeof(MEMORYSTATUSEX) };
    GlobalMemoryStatusEx(&memInfo);
    
    cout << "\nÉtat initial de la mémoire :" << endl;
    cout << "Mémoire totale     : " << (memInfo.ullTotalPhys / 1024 / 1024) << " MB" << endl;
    cout << "Mémoire utilisée   : " << fixed << setprecision(2) 
         << (100.0f - (memInfo.ullAvailPhys * 100.0f / memInfo.ullTotalPhys)) << "%" << endl;
    cout << "Mémoire disponible : " << (memInfo.ullAvailPhys / 1024 / 1024) << " MB" << endl;

    cout << "\nNettoyage en cours";
    for (int i = 0; i < 3; ++i) {
        cout << "." << flush;
        SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
        EmptyWorkingSet(GetCurrentProcess());
        this_thread::sleep_for(chrono::milliseconds(500));
    }
    cout << endl;

    GlobalMemoryStatusEx(&memInfo);
    cout << "\nAprès nettoyage :" << endl;
    cout << "Mémoire utilisée   : " << fixed << setprecision(2) 
         << (100.0f - (memInfo.ullAvailPhys * 100.0f / memInfo.ullTotalPhys)) << "%" << endl;
    cout << "Mémoire disponible : " << (memInfo.ullAvailPhys / 1024 / 1024) << " MB" << endl;
}

// Fonction pour nettoyer le cache CPU
void cleanCPUCache() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    
    cout << "\nInformations CPU :" << endl;
    cout << "Nombre de cœurs : " << sysInfo.dwNumberOfProcessors << endl;
    
    cout << "\nNettoyage du cache CPU en cours";
    vector<thread> threads;
    for (DWORD i = 0; i < sysInfo.dwNumberOfProcessors; ++i) {
        threads.emplace_back([]() {
            // Opération intensive pour nettoyer le cache
            vector<int> temp(1024 * 1024);
            for (int i = 0; i < 1024 * 1024; ++i) {
                temp[i] = i * 2;
            }
        });
    }
    
    for (int i = 0; i < 3; ++i) {
        cout << "." << flush;
        this_thread::sleep_for(chrono::milliseconds(500));
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    cout << "\nCache CPU nettoyé !" << endl;
}

// Fonction pour lister les ports ouverts (TCP)
void listOpenPorts() {
    cout << "\nPorts TCP ouverts :\n";
    
    PMIB_TCPTABLE pTcpTable;
    DWORD dwSize = 0;
    DWORD dwRetVal = 0;

    pTcpTable = (MIB_TCPTABLE*) malloc(sizeof(MIB_TCPTABLE));
    if (pTcpTable == nullptr) {
        cout << "Erreur d'allocation de mémoire\n";
        return;
    }

    dwSize = sizeof(MIB_TCPTABLE);
    // Récupérer les tables TCP
    if ((dwRetVal = GetTcpTable(pTcpTable, &dwSize, TRUE)) == ERROR_INSUFFICIENT_BUFFER) {
        free(pTcpTable);
        pTcpTable = (MIB_TCPTABLE*) malloc(dwSize);
        if (pTcpTable == nullptr) {
            cout << "Erreur d'allocation de mémoire\n";
            return;
        }
    }

    if ((dwRetVal = GetTcpTable(pTcpTable, &dwSize, TRUE)) == NO_ERROR) {
        cout << left << setw(10) << "Protocole" 
             << left << setw(20) << "Adresse Locale" 
             << left << setw(15) << "Port Local" 
             << left << setw(20) << "État" << endl;
        cout << string(65, '-') << endl;

        for (DWORD i = 0; i < pTcpTable->dwNumEntries; i++) {
            MIB_TCPROW row = pTcpTable->table[i];
            string state;
            switch (row.dwState) {
                case MIB_TCP_STATE_CLOSED: state = "Fermé"; break;
                case MIB_TCP_STATE_LISTEN: state = "Écoute"; break;
                case MIB_TCP_STATE_ESTAB: state = "Établi"; break;
                case MIB_TCP_STATE_CLOSE_WAIT: state = "Close Wait"; break;
                case MIB_TCP_STATE_FIN_WAIT1: state = "Fin Wait 1"; break;
                case MIB_TCP_STATE_FIN_WAIT2: state = "Fin Wait 2"; break;
                case MIB_TCP_STATE_LAST_ACK: state = "Last Ack"; break;
                case MIB_TCP_STATE_TIME_WAIT: state = "Time Wait"; break;
                case MIB_TCP_STATE_DELETE_TCB: state = "Delete TCB"; break;
                default: state = "Inconnu"; break;
            }

            cout << left << setw(10) << "TCP" 
                 << left << setw(20) << ipToString(row.dwLocalAddr) 
                 << left << setw(15) << ntohs(static_cast<u_short>(row.dwLocalPort)) 
                 << left << setw(20) << state << endl;
        }
    } else {
        cout << "Erreur lors de la récupération des tables TCP\n";
    }
    free(pTcpTable);
}

// Fonction pour lister les connexions réseau actives (TCP)
void listNetworkConnections() {
    cout << "\nConnexions réseau actives (TCP) :\n";
    
    PMIB_TCPTABLE pTcpTable;
    DWORD dwSize = 0;
    DWORD dwRetVal = 0;

    pTcpTable = (MIB_TCPTABLE*) malloc(sizeof(MIB_TCPTABLE));
    if (pTcpTable == nullptr) {
        cout << "Erreur d'allocation de mémoire\n";
        return;
    }

    dwSize = sizeof(MIB_TCPTABLE);
    // Récupérer les tables TCP
    if ((dwRetVal = GetTcpTable(pTcpTable, &dwSize, TRUE)) == ERROR_INSUFFICIENT_BUFFER) {
        free(pTcpTable);
        pTcpTable = (MIB_TCPTABLE*) malloc(dwSize);
        if (pTcpTable == nullptr) {
            cout << "Erreur d'allocation de mémoire\n";
            return;
        }
    }

    if ((dwRetVal = GetTcpTable(pTcpTable, &dwSize, TRUE)) == NO_ERROR) {
        cout << left << setw(10) << "Protocole" 
             << left << setw(20) << "Adresse Locale" 
             << left << setw(15) << "Port Local" 
             << left << setw(20) << "Adresse Distant" 
             << left << setw(15) << "Port Distant" 
             << left << setw(15) << "État" << endl;
        cout << string(95, '-') << endl;

        for (DWORD i = 0; i < pTcpTable->dwNumEntries; i++) {
            MIB_TCPROW row = pTcpTable->table[i];
            string state;
            switch (row.dwState) {
                case MIB_TCP_STATE_CLOSED: state = "Fermé"; break;
                case MIB_TCP_STATE_LISTEN: state = "Écoute"; break;
                case MIB_TCP_STATE_ESTAB: state = "Établi"; break;
                case MIB_TCP_STATE_CLOSE_WAIT: state = "Close Wait"; break;
                case MIB_TCP_STATE_FIN_WAIT1: state = "Fin Wait 1"; break;
                case MIB_TCP_STATE_FIN_WAIT2: state = "Fin Wait 2"; break;
                case MIB_TCP_STATE_LAST_ACK: state = "Last Ack"; break;
                case MIB_TCP_STATE_TIME_WAIT: state = "Time Wait"; break;
                case MIB_TCP_STATE_DELETE_TCB: state = "Delete TCB"; break;
                default: state = "Inconnu"; break;
            }

            cout << left << setw(10) << "TCP" 
                 << left << setw(20) << ipToString(row.dwLocalAddr) 
                 << left << setw(15) << ntohs(static_cast<u_short>(row.dwLocalPort)) 
                 << left << setw(20) << ipToString(row.dwRemoteAddr) 
                 << left << setw(15) << ntohs(static_cast<u_short>(row.dwRemotePort)) 
                 << left << setw(15) << state << endl;
        }
    } else {
        cout << "Erreur lors de la récupération des tables TCP\n";
    }
    free(pTcpTable);
}

// Fonction principale
int main() {
    auto tempMonitor = make_shared<TemperatureMonitor>();
    // Initialisation de Winsock (nécessaire pour certaines opérations réseau)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cout << "Échec de l'initialisation de Winsock.\n";
        return 1;
    }

    // Configuration de la console
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    this_thread::sleep_for(chrono::seconds(1));
    thread tempThread(drawTemperatureInfo, tempMonitor);
    tempThread.detach();

    char choice;
    while (true) {
        system("cls");
        ShowConsoleCursor(false);
        
        cout << R"(
╔══════════════════════════════════════════════════╗
║     ░██████╗██╗░░░░░░█████╗░░██████╗██╗░░██╗     ║
║     ██╔════╝██║░░░░░██╔══██╗██╔════╝██║░░██║     ║
║     ╚█████╗░██║░░░░░███████║╚█████╗░███████║     ║
║     ░╚═══██╗██║░░░░░██╔══██║░╚═══██╗██╔══██║     ║
║     ██████╔╝███████╗██║░░██║██████╔╝██║░░██║     ║
║     ╚═════╝░╚══════╝╚═╝░░╚═╝╚═════╝░╚═╝░░╚═╝     ║
╠══════════════════════════════════════════════════╣
║              Made by Slashy v1.0                 ║
╠══════════════════════════════════════════════════╣
║  [1] Nettoyer la mémoire RAM                     ║
║  [2] Nettoyer le cache CPU                       ║
║  [3] Scanner les processus                       ║
║  [4] Voir les ports ouverts                      ║
║  [5] Voir les connexions réseau                  ║
║  [6] Quitter                                     ║
╚══════════════════════════════════════════════════╝
)" << endl;

        ShowConsoleCursor(true);
        cout << "Votre choix (1-6): ";
        cin >> choice;
        cin.ignore(1000, '\n');

        switch (choice) {
            case '1':
                cleanRAM();
                break;

            case '2':
                cleanCPUCache();
                break;

            case '3': {
                cout << "\nLancement du scan des processus...\n" << endl;
                auto processes = scanProcesses();

                sort(processes.begin(), processes.end(), 
                     [](const ProcessInfo& a, const ProcessInfo& b) {
                         return a.memoryUsage > b.memoryUsage;
                     });

                cout << "\nRésultats du scan :" << endl;
                cout << left << setw(40) << "Nom"
                     << right << setw(15) << "PID"
                     << right << setw(20) << "Mémoire (MB)" << endl;
                cout << string(75, '-') << endl;

                for (const auto& p : processes) {
                    if (p.suspicious) {
                        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                    }
                    
                    cout << left << setw(40) << p.name 
                         << right << setw(15) << p.pid
                         << right << setw(20) << fixed << setprecision(2) << (p.memoryUsage / 1024.0 / 1024.0);
                    
                    if (p.suspicious) {
                        cout << " [SUSPECT]";
                        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                    }
                    cout << endl;
                }
                break;
            }
            case '4':
                listOpenPorts();
                break;
            case '5':
                listNetworkConnections();
                break;
            case '6':
                system("cls");
                cout << "Au revoir !" << endl;
                WSACleanup();  // Nettoyer Winsock
                return 0;

            default:
                cout << "Option invalide !" << endl;
                break;
        }

        cout << "\nAppuyez sur ENTRÉE pour continuer...";
        cin.get();
    }

    WSACleanup();
    return 0;
}
