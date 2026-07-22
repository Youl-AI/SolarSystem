#include "CelestialData.hpp"

    // 🚀 [추가] 천체 정보를 반환하는 도서관 함수
    std::string getCelestialInfo(const std::string& name) {
        // 1. 항성 및 지구형 행성
        if (name == "Sun") return "Diameter: 1,392,700 km\nMass: 1.989 x 10^30 kg\nSurface Temp: 5,500 °C\nType: Yellow Dwarf (G2V)";
        if (name == "Mercury") return "Diameter: 4,879 km\nMass: 3.30 x 10^23 kg\nGravity: 3.7 m/s^2\nDay Length: 58d 15h";
        if (name == "Venus") return "Diameter: 12,104 km\nMass: 4.87 x 10^24 kg\nGravity: 8.87 m/s^2\nSurface Temp: 462 °C";
        if (name == "Earth") return "Diameter: 12,742 km\nMass: 5.97 x 10^24 kg\nGravity: 9.8 m/s^2\nSurface Temp: 14 °C";
        if (name == "Mars") return "Diameter: 6,779 km\nMass: 6.39 x 10^23 kg\nGravity: 3.71 m/s^2\nMoons: Phobos, Deimos";
        
        // 2. 목성형 행성 (가스/얼음 거성)
        if (name == "Jupiter") return "Diameter: 139,820 km\nMass: 1.89 x 10^27 kg\nGravity: 24.79 m/s^2\nType: Gas Giant";
        if (name == "Saturn") return "Diameter: 116,460 km\nMass: 5.68 x 10^26 kg\nGravity: 10.44 m/s^2\nType: Gas Giant";
        if (name == "Uranus") return "Diameter: 50,724 km\nMass: 8.68 x 10^25 kg\nGravity: 8.69 m/s^2\nType: Ice Giant";
        if (name == "Neptune") return "Diameter: 49,244 km\nMass: 1.02 x 10^26 kg\nGravity: 11.15 m/s^2\nType: Ice Giant";

        // 3. 주요 위성 (지구, 목성, 토성)
        if (name == "Moon") return "Diameter: 3,474 km\nMass: 7.34 x 10^22 kg\nGravity: 1.62 m/s^2\nType: Earth's Moon";
        if (name == "Io") return "Diameter: 3,642 km\nMass: 8.93 x 10^22 kg\nGravity: 1.79 m/s^2\nType: Galilean Moon (Volcanic)";
        if (name == "Europa") return "Diameter: 3,121 km\nMass: 4.79 x 10^22 kg\nGravity: 1.31 m/s^2\nType: Galilean Moon (Ice Ocean)";
        if (name == "Ganymede") return "Diameter: 5,268 km\nMass: 1.48 x 10^23 kg\nGravity: 1.42 m/s^2\nType: Galilean Moon (Largest)";
        if (name == "Callisto") return "Diameter: 4,820 km\nMass: 1.07 x 10^23 kg\nGravity: 1.23 m/s^2\nType: Galilean Moon (Cratered)";
        if (name == "Titan") return "Diameter: 5,149 km\nMass: 1.34 x 10^23 kg\nGravity: 1.35 m/s^2\nType: Saturnian Moon (Atmosphere)";

        // 4. 왜소행성 (소행성대 및 카이퍼 벨트)
        if (name == "Ceres") return "Diameter: 939 km\nMass: 9.39 x 10^20 kg\nGravity: 0.28 m/s^2\nType: Dwarf Planet (Asteroid Belt)";
        if (name == "Pluto") return "Diameter: 2,376 km\nMass: 1.30 x 10^22 kg\nGravity: 0.62 m/s^2\nType: Dwarf Planet (Kuiper Belt)";
        if (name == "Haumea") return "Diameter: ~1,632 km\nMass: 4.01 x 10^21 kg\nGravity: 0.40 m/s^2\nType: Dwarf Planet (Oblong)";
        if (name == "Makemake") return "Diameter: 1,430 km\nMass: ~3.1 x 10^21 kg\nGravity: 0.50 m/s^2\nType: Dwarf Planet";
        if (name == "Eris") return "Diameter: 2,326 km\nMass: 1.66 x 10^22 kg\nGravity: 0.82 m/s^2\nType: Dwarf Planet (Scattered Disc)";

        // 예외 처리 (엔진에 추가될 미지의 소행성 대비)
        return "Diameter: N/A\nMass: Unknown\nGravity: Unknown\nType: Celestial Body";
    }
