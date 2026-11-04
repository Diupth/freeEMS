# Kế hoạch xây dựng phần mềm mô phỏng siêu cao tần

## 1. Mục tiêu

Xây dựng ứng dụng mô phỏng điện từ 3D chạy trên Windows, Linux và macOS, hướng tới các bài toán siêu cao tần như anten và ống dẫn sóng. Phần mềm phải có công cụ dựng và chỉnh sửa mô hình 3D ngay trong ứng dụng. Dùng C++ làm ngôn ngữ chính cho ứng dụng để tích hợp CAD/đồ họa và có khả năng tối ưu hiệu năng; ưu tiên tích hợp solver mã nguồn mở thay vì tự xây bộ giải từ đầu.

Mục tiêu ban đầu là một công cụ chuyên dụng, có quy trình mô phỏng đáng tin cậy và hiệu năng được đo lường trên CPU/GPU; chưa đặt mục tiêu thay thế toàn diện CST hoặc HFSS.

## Tiến độ và kết quả giai đoạn 1–3

### Giai đoạn 1 — khảo sát kỹ thuật

**Trạng thái: Hoàn tất khảo sát tài liệu và rà soát máy macOS; chưa kiểm thử trên Windows/Linux hoặc benchmark solver.**

| Hạng mục | Kết quả và hướng đề xuất |
|---|---|
| Ngôn ngữ/build | Chọn C++ và CMake làm hướng chính; Qt/OCCT/VTK đều có quy trình native C++ |
| CAD | OCCT là ứng viên dựng solid/Boolean và trao đổi CAD; rà soát đúng license/exception khi phân phối |
| Solver | Ưu tiên thử openEMS cho RF; macOS cần cài thủ công/build từ source. MEEP là đối chứng nhưng binary Conda không chạy native Windows |
| Mesh | Gmsh tạo được lưới từ CAD, nhưng license GPL-2.0-or-later kèm exception cần được rà soát trước khi đóng gói |
| GUI/đồ họa | Qt 6 + VTK là ứng viên; cần chốt module Qt và nghĩa vụ license cụ thể |
| CPU/GPU | CPU đa luồng là mục tiêu trước; chưa xác minh backend GPU upstream đủ ổn định cho yêu cầu đa nền tảng |

- [x] Rà soát môi trường macOS ARM64 trên Apple M2, GPU Apple/Metal 4; có CMake 4.3.1, Apple Clang 21 và Git.
- [x] Xác nhận không có `nvidia-smi`/`nvcc`; CUDA chưa thể kiểm chứng trên máy này.
- [ ] Kiểm thử build/chạy trên Windows và Linux.
- [ ] Benchmark solver CPU/GPU và xác minh backend GPU cụ thể.

### Giai đoạn 2 — prototype CAD/mesh

**Trạng thái: Đã nối prototype CAD → openEMS Cartesian/CSXCAD, GUI nhiều solid và xem S11/dạng sóng; vẫn là prototype nghiên cứu, chưa kiểm thử Windows/Linux theo yêu cầu hiện tại.**

- [x] C++/CMake prototype dùng OCCT dựng tấm 40 × 20 × 1.6 mm có lỗ tròn đường kính 6 mm, kiểm tra hình học và xuất STEP.
- [x] CMake target `prototype_mesh` tạo lưới 3D MSH4 từ STEP bằng Gmsh.
- [x] Build trên macOS ARM64 với Apple Clang 21, CMake 4.3.1, OCCT 7.9.3 và Gmsh 4.15.2.
- [x] Kiểm tra mesh: 196 nodes, 963 elements; không phát hiện duplicate nodes/elements hoặc isolated nodes.
- [x] Cài OCCT và Gmsh bằng Homebrew; Homebrew cũng nâng cấp một số dependency đã cài.
- [x] Tạo GUI Qt C++ cho phép chỉnh kích thước mô hình tấm có lỗ, xoay xem trước và xuất STEP; build và smoke test offscreen thành công.
- [x] Thêm giao diện chạy tiến trình openEMS riêng, chọn CPU/GPU, chọn thư mục kết quả và xem log; build và GUI smoke test thành công.
- [x] Thêm script bài toán anten patch tham chiếu, xuất S11 CSV và thời gian chạy; kiểm tra cú pháp Python.
- [x] Tạo thư mục chạy riêng theo từng lần mô phỏng để không ghi đè dữ liệu solver trung gian.
- [x] Build openEMS v0.37.0-38-gd686f1d và Python bindings cục bộ; xác nhận import `openEMS`, `CSXCAD` và chạy anten patch CPU thành công.
- [x] Chạy cùng bài toán patch bằng backend Metal trên Apple M2; log xác nhận `GPU engine` và `Metal (Apple M2)`.
- [x] So sánh 201 điểm S11: sai khác phức lớn nhất giữa CPU/GPU là 5.77e-7; cả hai đạt ngưỡng dừng -40 dB sau 11,461 bước.
- [x] Ghi benchmark tham chiếu: CPU 1.01 s, Metal GPU 2.36 s (tính theo thời gian script, gồm thiết lập); GPU chậm hơn với mô hình nhỏ 15,939 cells này.
- [x] Nối hình học OCCT sang STL kín, nạp bằng CSXCAD `PolyhedronReader`, ánh xạ lên lưới Cartesian/Yee của openEMS; chạy xác nhận có PEC edges từ solid CAD.
- [x] GUI truyền từng solid đang chỉnh sửa sang tiến trình solver dưới dạng STL; xuất compound STEP riêng; có cấu hình vị trí cổng Z.
- [x] Mở rộng CAD editor với danh sách nhiều plate-with-hole/box/cylinder, chọn qua danh sách, sửa kích thước/vị trí và xuất compound STEP.
- [x] Tạo tab đồ thị S11 và dạng sóng điện áp tới/phản xạ; xuất CSV, JSON summary và cho phép mở thư mục kết quả.
- [x] Chia GUI thành các tab 3D Editor, Materials, Simulation và Results; chỉnh per-solid PEC/điện môi (εr, σ, tanδ), dải tần, số điểm S11, mật độ lưới, số bước thời gian, biên, trở kháng và vị trí cổng; thông số được chuyển vào openEMS.
- [x] Kiểm thử end-to-end qua GUI headless: GUI xuất STL, sinh scene JSON, gọi openEMS CPU, thoát thành công và tạo 201 điểm S11 cùng 59 mẫu waveform.
- [x] Kiểm tra cấu hình điện môi, dải 1.5–2.5 GHz, 51 điểm, biên PML_8 và cổng 75 Ω qua solver.
- [x] Chạy cùng STL scene trên CPU và Metal GPU; mỗi lần tạo 201 điểm S11 và 65 mẫu dạng sóng. Sai khác S11 tuyệt đối tối đa 2.24e-7; thời gian CPU 0.163 s, GPU 0.275 s trên scene 17,081 cells.
- [x] Tạo và chạy bài toán tham chiếu ống dẫn sóng WR90 thẳng, TE10, 8.2–12.4 GHz bằng hai lần kích thích openEMS; xuất S11/S21/S12/S22, VSWR hai cổng, waveform điện áp cổng và JSON summary.
- [x] Thêm nút chạy WR90 vào GUI; tab Results vẽ đồng thời S11/S21, VSWR hai cổng và waveform. Solver dùng kích thước WR90 mặc định 22.86 × 10.16 × 100 mm, lưới 30 cells/λ và hai modal waveguide ports TE10.
- [x] Tổ chức lại GUI theo workflow kiểu CST: menu File/Home/Modeling/Simulation/Post-Processing/View, ribbon, Navigation Tree dạng dock, viewport 3D, parameter list, Results và dock Progress / Messages; schematic được tạm ẩn theo yêu cầu giao diện.
- [x] Cho phép kéo thay đổi kích thước Navigation Tree, Progress / Messages, panel thuộc tính và 3D viewport; thêm công cụ Draw Plate/Draw Box bằng kéo chuột trong Top View và kiểm thử mouse-drag tổng hợp.
- [x] Hiển thị marker lumped port trong viewport, đồng bộ với tọa độ đầu/cuối và trục X/Y/Z ở Simulation Setup; thêm hướng dẫn thao tác.
- [x] GUI tự nạp kết quả mới nhất khi khởi động từ `build/artifacts/wr90`, đồng thời tìm dữ liệu tham chiếu cũ trong `build/artifacts/wr90-test`. Bổ sung lệnh Load WR90 results để mở thư mục khác.
- [x] Chạy WR90 end-to-end qua tiến trình GUI headless; xác nhận các CSV được tạo tại thư mục mặc định và nạp được vào Results.
- [ ] Kiểm thử Windows/Linux (tạm hoãn theo yêu cầu người dùng) và benchmark trên mô hình lớn hơn; kết quả GPU hiện chỉ áp dụng cho backend Metal trên M2 và không cho thấy lợi ích tốc độ ở mô hình nhỏ.

**Phát hiện kỹ thuật:** CAD B-Rep được tam giác hóa thành STL kín rồi CSXCAD rasterize lên lưới Cartesian/Yee; Gmsh tetra mesh vẫn không được dùng trực tiếp bởi openEMS.

GUI được tổ chức theo kiểu CST ở cấp workflow: Menu Bar, Ribbon icon kèm nhãn, Navigation Tree dock bên trái và Parameter List/Progress/Messages ở dock đáy; Properties và viewport được chia bằng splitter. Panel trung tâm mặc định hiển thị 3D, không có tab; chọn Geometry, Materials, Excitations hoặc Results trong Navigation Tree để đổi nội dung. Schematic tạm thời không hiển thị. Dock và splitter kéo đổi kích thước được, bố cục được lưu/khôi phục và có Reset Layout. Hướng dẫn chi tiết ở [instruction.md](./instruction.md).

Modeling hỗ trợ kéo vẽ, import STEP, Boolean fuse/cut, dịch chuyển/xoay, chọn trực tiếp, grid Top View và reset camera; OCCT validity được kiểm tra trước khi xuất STL/solver. STEP/Boolean solids được giữ dưới dạng B-Rep cho solver/export; viewport Qt hiện vẽ chúng bằng bounding-box proxy, chưa render topology chính xác. Port CAD hiện là một lumped port với hai endpoint dọc trục X/Y/Z; WR90 modal guide có lựa chọn TE10/TE20/TE01/TE11 và kiểm tra cutoff; TE10/TE20 đã chạy đối chiếu, TE01/TE11 chưa benchmark. Vẫn chưa có danh sách nhiều lumped port CAD, port coaxial/discrete/Floquet hoặc nguồn tùy ý.

Bài patch tham chiếu có S11/VSWR, lát cắt gần trường E/H dạng HDF5+CSV và NF2FF radiation pattern; đây là bài tham chiếu, chưa phải far-field solver cho mọi scene CAD. Chạy patch qua nút Run Patch Antenna; chạy WR90 qua nút Run WR90. Solver từ chối lưới vượt 5 triệu cells mặc định và summary ghi số cell cùng ước lượng bộ nhớ (72 bytes/cell, chỉ là ước tính). Bộ kiểm thử `tests/reference_expectations.json` đặt ngưỡng cho đường truyền WR90, độ phản xạ/reciprocity, kết quả patch và far-field; các ngưỡng rộng nhằm bắt lỗi đơn vị/cấu hình lớn, không thay thế certification. Đo trên máy hiện tại: WR90 TE10 201 điểm, max S11 −43.67 dB, reciprocity error 6.97e−6 dB; TE20 sweep 13.5–15 GHz (cutoff 13.114 GHz), max S11 −26.63 dB, max S21 +0.0059 dB. Patch 1–3 GHz: min S11 −18.69 dB, peak directivity 5.82 dBi, near-field slice 462 samples. Đây là các kiểm thử tham chiếu; chưa đại diện cho mọi vật liệu/cổng/CAD. Kết quả WR90 tự nạp từ `build/artifacts/wr90`, còn patch lưu tại `build/artifacts/antenna`; tab Results có Load WR90/Antenna results. Viewport vẫn là Qt projected renderer; trường E/H hiện xuất lát cắt phẳng, không có transient animation hay volumetric interaction. GPU chậm hơn CPU ở scene nhỏ này.

### Giai đoạn 3 — MVP (đang triển khai)

- [x] Vẽ footprint hình chữ nhật của box/plate bằng thao tác kéo chuột trong Top View; hiệu chỉnh kích thước/vị trí qua Properties, chọn và nhân đôi đối tượng.
- [x] Lưu/mở dự án `.rfsim.json`, gồm solid, vật liệu và thiết lập solver; định dạng có version và kiểm tra đầu vào trước khi áp dụng.
- [x] Hiển thị tiến trình solver dạng busy indicator, log và nút hủy tiến trình; kiểm thử round-trip dự án, thao tác vẽ và hủy tiến trình.
- [x] Giữ workflow CPU/GPU và xuất STEP; hỗ trợ vật liệu PEC/điện môi, cấu hình tần số, lưới, biên và một lumped port định hướng X/Y/Z.
- [x] Import STEP; Boolean fuse/cut; rotate/move; chọn solid trong viewport; grid/camera; kiểm tra B-Rep validity trước khi tạo lưới.
- [x] Hỗ trợ CAD lumped port theo trục X/Y/Z và modal WR90 TE modes; kiểm tra cutoff; bài patch reference xuất S11, near-field E/H slice (HDF5/CSV), far-field pattern/CSV.
- [x] Thêm benchmark fixtures và ngưỡng kiểm tra WR90 TE10/TE20, antenna patch; giới hạn solver mặc định 5M cells, log memory estimate, kiểm tra resource rejection và cancel/error path.
- [x] 14/14 CTest pass, gồm import/Boolean/transform round-trip, direct viewport pick, X-axis port, antenna end-to-end, resource guard và các reference fixtures.
- [x] Sắp xếp lại theo hướng dẫn UI: Navigation Tree bên trái, Parameter List/Progress/Messages dock đáy, icon kèm nhãn trong Ribbon, menu Tools/Help và lưu/reset bố cục; schematic hiện tạm ẩn.
- [x] Viết [instruction.md](./instruction.md) hướng dẫn tạo dự án, vẽ, port, vật liệu, setup, chạy và đọc kết quả.
- [ ] Hỗ trợ nhiều CAD ports/source tùy ý, port coaxial/discrete/Floquet, trường 3D/transient, near/far field cho arbitrary CAD scenes và thêm bài benchmark nhiều vật liệu/cấu hình.
- [ ] Build/test Windows/Linux (đang hoãn theo yêu cầu); đánh giá đóng gói và giấy phép dependency trước phát hành.

Chạy ứng dụng bằng `./build/rf_sim_gui`. Dự án lưu/mở bằng File → Save/Open Project, định dạng JSON versioned `.rfsim.json`; hình học có thể xuất STEP. Solver có Cancel simulation và log/progress ở dock dưới. Chạy `ctest --test-dir build --output-on-failure` để kiểm tra workflow C++; `QT_QPA_PLATFORM=offscreen ./build/rf_sim_gui --smoke-solver` kiểm thử solver CAD end-to-end. Các bài kiểm tra này không thay thế thiết lập port/vật liệu có ý nghĩa vật lý.

Prototype: [CMakeLists.txt](./CMakeLists.txt), [src/main.cpp](./src/main.cpp), [src/gui/main.cpp](./src/gui/main.cpp), [src/gui/ModelView.cpp](./src/gui/ModelView.cpp), [scripts/openems_patch.py](./scripts/openems_patch.py). Kết quả mesh Gmsh ban đầu: [rf_plate.step](./build/artifacts/rf_plate.step), [rf_plate.msh](./build/artifacts/rf_plate.msh).

```sh
cmake -S . -B build -DOpenCASCADE_DIR=/opt/homebrew/opt/opencascade/lib/cmake/opencascade
cmake --build build --target prototype_mesh --parallel 4
gmsh build/artifacts/rf_plate.msh -check -v 5
```

## 2. Phạm vi MVP

### Bao gồm

- Dựng/chỉnh sửa hình học 3D: hộp, trụ, mặt phẳng, lỗ, dịch chuyển, xoay, sao chép, hợp nhất và trừ khối.
- Chọn đối tượng trong khung nhìn, điều khiển camera, lưới tham chiếu và nhập tọa độ/kích thước chính xác.
- Gán vật liệu và thuộc tính mô phỏng cho các đối tượng.
- Nhập/xuất định dạng hình học phổ biến để trao đổi mô hình.
- Khai báo vật liệu: điện môi, kim loại, độ dẫn điện và một số vật liệu tổn hao.
- Cấu hình cổng kích thích, nguồn, điều kiện biên và dải tần.
- Chạy mô phỏng FDTD 3D trên CPU; theo dõi tiến độ và hủy tác vụ.
- Hiển thị S-parameters, trường E/H, đồ thị tần số và lát cắt trường.
- Lưu/mở dự án, thiết lập lại mô phỏng và xuất dữ liệu.

### Chưa bao gồm

- CAD cơ khí tổng quát, sketch/constraint parametric nâng cao và thao tác bề mặt phức tạp.
- Tối ưu hóa tự động, mô phỏng đa vật lý.
- Nhiều phương pháp giải hoặc tương thích đầy đủ với định dạng CST/HFSS.
- GPU là yêu cầu bắt buộc cho MVP; chỉ cam kết accelerator sau khi đo tính khả thi, độ chính xác và khả năng đóng gói.

## 3. Thành phần mã nguồn mở cần đánh giá

| Thành phần | Ứng viên | Việc cần xác minh |
|---|---|---|
| Solver FDTD | openEMS, MEEP | Độ phù hợp với workflow; cài đặt, đóng gói, hiệu năng và hỗ trợ thực tế trên ba hệ điều hành |
| CAD 3D | Open CASCADE Technology (OCCT) | Kernel solid, Boolean, chọn đối tượng, nhập/xuất CAD; kiểm tra giấy phép, API và đóng gói |
| Hình học và lưới | Gmsh hoặc công cụ lưới phù hợp | Chuyển hình học CAD sang lưới solver hỗ trợ; chất lượng ở cạnh cong/khe hẹp |
| Giao diện | Qt/C++ | Dùng C++ xuyên suốt ứng dụng desktop; kiểm tra module cần dùng, giấy phép và cách phân phối |
| Trực quan hóa | VTK/ParaView hoặc thư viện đồ họa 3D | Tái sử dụng cho khung nhìn CAD và hiển thị trường; kiểm tra backend đồ họa trên ba hệ điều hành |
| Build và dependency C++ | CMake, vcpkg hoặc Conan | Chọn một quy trình build/package có thể tái lập trên Windows, Linux và macOS |
| Tính toán CPU/GPU | Backend solver; OpenMP hoặc lớp đa backend như Kokkos/SYCL nếu cần | Xác minh solver/backend, thiết bị, driver, build và giấy phép theo hệ điều hành |

OCCT là ứng viên cho kernel CAD 3D nhúng. Cần prototype vì CAD solid và hình học/mesh dùng cho solver là hai lớp riêng. Không quyết định stack chỉ từ danh sách thư viện: xác minh phiên bản, hoạt động đa nền tảng và nghĩa vụ giấy phép trước khi chốt phương án phát hành.

## 4. Kiến trúc đề xuất

```text
Khung nhìn 3D + công cụ dựng hình (CAD kernel)
        │
Mô hình dự án / hình học / vật liệu
        │
Kiểm tra hình học → tạo lưới → bộ chuyển đổi solver
        │
Tiến trình solver riêng (CPU hoặc accelerator)
        │
Đọc kết quả → đồ thị/trường điện từ
```

- Tách hình học CAD (B-Rep) khỏi biểu diễn tính toán/lưới; không để giao diện phụ thuộc định dạng nội bộ của solver.
- Dựng hình bằng các thao tác tham số cơ bản và Boolean; lưu thuộc tính đủ để chọn và chỉnh sửa đối tượng.
- Chạy solver trong tiến trình riêng để giao diện không bị treo, có thể báo tiến độ, hủy tác vụ và lưu log.
- Tạo adapter cho solver để có thể thay thế/bổ sung solver mà không viết lại giao diện.
- Lưu dự án dạng JSON/YAML có phiên bản schema; lưu kết quả riêng với metadata ứng dụng, solver và tham số chạy.
- Hiển thị lỗi solver và lỗi đầu vào rõ ràng.

## 5. Nghiên cứu và tối ưu hiệu năng CPU/GPU

### Nguyên tắc

- Làm cho đường chạy CPU đa luồng ổn định trước; CPU là backend mặc định và luôn là phương án dự phòng.
- C++ giúp kiểm soát bộ nhớ, tích hợp thư viện native và tối ưu kernel, nhưng tự nó không bảo đảm solver nhanh hơn; thuật toán, cấu trúc dữ liệu và băng thông bộ nhớ mới quyết định hiệu năng.
- Đo hiệu năng bằng mô hình chuẩn, không chọn GPU chỉ dựa trên thông số lý thuyết.
- So sánh kết quả số giữa backend CPU/GPU trong sai số được xác định trước.
- Không tự viết backend GPU cho solver nếu chưa chứng minh có đường tích hợp khả thi; việc này có thể làm tăng đáng kể phạm vi.

### Kế hoạch đo và tối ưu

1. Chọn 2–3 mô hình đại diện với lưới nhỏ, vừa và lớn; ghi thời gian chạy, RAM/VRAM, số ô lưới và sai số.
2. Lập baseline theo hệ điều hành và phần cứng; tách thời gian dựng hình/mesh, solver và hậu xử lý.
3. Với CPU, profiling trước khi tối ưu; khảo sát đa luồng (ví dụ OpenMP nếu phù hợp), SIMD, locality/cache và giới hạn băng thông bộ nhớ của solver.
4. Với GPU, xác minh solver có backend được duy trì hay không, danh sách GPU/API hỗ trợ, yêu cầu driver, giới hạn bộ nhớ và khả năng build đa nền tảng.
5. Đo overhead truyền dữ liệu CPU↔GPU, thời gian khởi tạo và mức tăng tốc đầu-cuối trên bài toán nhỏ lẫn lớn.
6. Nếu phải viết/tùy biến kernel, giữ backend CPU làm chuẩn đối chiếu; benchmark và kiểm thử sai số tự động trên mỗi backend.
7. Chỉ đưa GPU vào bản phát hành khi lợi ích đo được lớn hơn chi phí hỗ trợ thiết bị/driver và có đường fallback rõ ràng.

### Tiêu chí quyết định GPU

- Tăng tốc đầu-cuối trên các bài toán mục tiêu, không chỉ tốc độ kernel.
- Không vượt giới hạn RAM/VRAM đã thống nhất; thông báo khi bài toán quá lớn.
- Kết quả nằm trong sai số so với bộ kiểm thử tham chiếu.
- Quy trình cài đặt, driver và backend được tài liệu hóa theo hệ điều hành.
- Nếu không đạt các tiêu chí, phát hành bản CPU đa luồng trước và giữ GPU trong roadmap.

## 6. Các giai đoạn triển khai

| Giai đoạn | Ước lượng | Kết quả |
|---|---:|---|
| Khảo sát kỹ thuật | 3–4 tuần | Nghiên cứu tài liệu và rà soát môi trường macOS đã hoàn tất; prototype, benchmark và xác minh trên Windows/Linux còn chờ |
| Prototype CAD + solver | 4–6 tuần | Tạo/chỉnh sửa solid 3D, chuyển hình học sang biểu diễn solver tương thích, chạy bài toán chuẩn, đọc S-parameters và đối chiếu kết quả |
| MVP | 12–18 tuần | Công cụ dựng hình 3D cơ bản, vật liệu/biên/cổng, solver CPU đa luồng, biểu đồ/trường, dự án và xuất dữ liệu |
| Tối ưu accelerator | 4–10 tuần khảo sát/triển khai | Đánh giá backend GPU, benchmark đầu-cuối; triển khai/tích hợp nếu đạt tiêu chí hỗ trợ |
| Ổn định đa nền tảng | 4–8 tuần | Bộ cài cho ba hệ điều hành, kiểm thử tự động, kiểm tra đầu vào và xử lý lỗi |
| Mở rộng | Theo nhu cầu | CAD nâng cao, mesh controls, thêm solver hoặc FEM/MoM khi MVP chứng minh được nhu cầu |

Ước lượng dành cho nhóm nhỏ; giai đoạn GPU có thể chạy song song với MVP nhưng chỉ nên cam kết sau prototype. Tích hợp CAD và đóng gói solver đa nền tảng có thể làm thay đổi đáng kể tiến độ.

## 7. Rủi ro và cách giảm thiểu

- **Cài đặt solver trên macOS/Windows:** thử ngay trong giai đoạn khảo sát, không đợi đến cuối dự án.
- **CAD kernel và solver không khớp:** prototype luồng CAD → hình học hợp lệ → mesh → solver trước khi đầu tư vào công cụ dựng hình đầy đủ.
- **Giấy phép:** lập bảng dependency, phiên bản, giấy phép và cách liên kết trước khi quyết định phân phối.
- **Độ tin cậy mô phỏng:** tạo bộ kiểm thử chuẩn có kết quả tham chiếu; không chỉ kiểm tra chương trình chạy thành công.
- **Hiệu năng CPU/GPU:** benchmark trên phần cứng đại diện, tính cả truyền dữ liệu, bộ nhớ và thời gian tiền/hậu xử lý.
- **GPU và driver:** giữ CPU làm fallback; không quảng bá hỗ trợ GPU toàn diện nếu chưa xác minh.
- **Phạm vi CAD:** ưu tiên công cụ dựng solid phục vụ workflow RF, không cố làm CAD cơ khí tổng quát trong MVP.
- **Phạm vi sản phẩm:** tập trung vào workflow có thể kiểm chứng thay vì cam kết thay thế CST/HFSS toàn diện.

## 8. Bước tiếp theo

Tạo prototype end-to-end cho cùng một bài toán anten hoặc ống dẫn sóng:

1. Dùng OCCT thử dựng/chỉnh sửa một solid cơ bản và xuất hình học.
2. Quyết định luồng phù hợp: hình học/lưới Cartesian cho openEMS hoặc Gmsh tetra mesh cho solver FEM phù hợp.
3. Chạy solver cho một bài toán chuẩn, đọc S-parameters và đối chiếu với kết quả tham chiếu.
4. Cài/chạy trên Windows, Linux và macOS; benchmark CPU và xác minh GPU/backend được hỗ trợ.
5. Hoàn tất đánh giá license và cách đóng gói CAD kernel, solver, mesh và GUI C++.

## 9. Tiêu chí hoàn thành MVP

- Cài đặt và khởi chạy được trên Windows, Linux và macOS theo quy trình được tài liệu hóa.
- Người dùng dựng, chọn và chỉnh sửa được mô hình solid 3D cơ bản ngay trong ứng dụng; lưu/mở được dự án.
- Mô hình được kiểm tra, tạo lưới và chuyển sang một workflow mô phỏng đã hỗ trợ.
- Solver chạy được, báo tiến độ, hủy tác vụ và báo lỗi rõ ràng.
- Kết quả S-parameters và trường điện từ được hiển thị hoặc xuất ra.
- Bộ bài toán kiểm thử chuẩn cho kết quả nằm trong sai số đã thống nhất.
- Có benchmark CPU; GPU chỉ được đánh dấu hỗ trợ nếu đạt tiêu chí hiệu năng, độ chính xác và triển khai.
- Dependency và nghĩa vụ giấy phép được ghi nhận trước khi phát hành.

## 10. Tài liệu tham khảo

- [Open CASCADE Technology documentation](https://dev.opencascade.org/doc/) — CAD kernel, modeling và Boolean operations.
- [OCCT build guide](https://dev.opencascade.org/doc/overview/html/building_overview.html) và [license](https://dev.opencascade.org/doc/overview/html/occt_dev_guides__license.html).
- [openEMS installation documentation](https://docs.openems.de/en/latest/install/index.html) — kiểm tra lại tình trạng hỗ trợ nền tảng/accelerator theo phiên bản thử nghiệm.
- [MEEP installation](https://meep.readthedocs.io/en/latest/Installation/) và [parallel support](https://meep.readthedocs.io/en/latest/Features/#parallel-meep).
- [Gmsh reference manual](https://gmsh.info/doc/texinfo/gmsh.html) và [license](https://gmsh.info/).
- [Qt supported platforms](https://doc.qt.io/qt-6/supported-platforms.html), [CMake](https://doc.qt.io/qt-6/cmake-manual.html) và [licensing](https://doc.qt.io/qt-6/licensing.html).
- [VTK build documentation](https://docs.vtk.org/en/latest/build_instructions/build.html) và [C++/CMake guide](https://docs.vtk.org/en/latest/getting_started/using_cpp.html).

Các kết luận về hỗ trợ nền tảng/GPU là kết quả rà soát tài liệu ban đầu, không thay thế build và benchmark với phiên bản dependency cụ thể. Thông tin GPU không được giả định; cần xác minh trực tiếp trước khi chốt kiến trúc.
