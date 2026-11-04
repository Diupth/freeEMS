# Hướng dẫn sử dụng RF Simulation

Tài liệu này mô tả các chức năng hiện có trong prototype. Giao diện được
tham khảo cách tổ chức CAD/CAE của CST, nhưng không phải bản sao CST và
chưa hỗ trợ toàn bộ các loại solver, port hay kết quả trường.

## 1. Khởi chạy và bố cục

Build và mở ứng dụng:

```sh
cmake -S . -B build -DOpenCASCADE_DIR=/opt/homebrew/opt/opencascade/lib/cmake/opencascade
cmake --build build -j4
./build/rf_sim_gui
```

Các vùng chính:

- **Menu Bar:** File, Home, Modeling, Simulation, Post-Processing, View,
  Tools và Help.
- **Ribbon:** các nút có cả icon lẫn nhãn; chọn tab để xem công cụ theo
  công việc. Tab **Simulation** có dropdown **Solver Selection** với
  **openEMS**, **MEEP**, **PALACE** và **NEC2**; dropdown **Domain** có
  **Frequency Domain Solver** và **Time Domain Solver**, mỗi lựa chọn có icon
  riêng. Các solver ngoài openEMS và lựa chọn miền hiện mới là điều khiển giao
  diện, chưa được nối vào backend mô phỏng. **Solver Options** hiện chưa có nội
  dung và chưa thực hiện hành động; cấu hình solver sẽ được bổ sung sau.
- **Navigation Tree:** Geometry, Materials, Excitations và Results. Chọn mục
  để hiển thị nội dung tương ứng ở panel trung tâm; chọn solid trong Geometry
  hoặc click vào solid trong viewport để chỉnh sửa.
- **3D Viewport:** xem, vẽ và chọn hình học. Solid STEP/Boolean hiện được
  hiển thị bằng bounding-box proxy; solver và xuất STEP sử dụng B-Rep thật.
- **Panel trung tâm:** mặc định hiển thị 3D Viewport; schematic tạm thời không
  hiển thị.
- **Parameter List:** bảng chỉ đọc các thông số mô phỏng đang dùng.
- **Progress / Messages:** trạng thái, progress indicator, log và lỗi solver.
- **Status Bar:** trạng thái ứng dụng và đơn vị hiện dùng (mm, GHz, ns, deg).

Kéo đường phân cách giữa viewport và Parameter List/Progress để đổi chiều cao
panel dưới; kéo đường phân cách Navigation Tree hoặc Properties/viewport để
đổi chiều rộng. Ứng dụng mở ở chế độ maximize để vừa màn hình; **View → Reset
Layout** khôi phục kích thước panel mặc định.

## 2. Tạo, mở và lưu dự án

1. Chọn **File → New Project** để bắt đầu dự án trống. App sẽ hỏi xác nhận
   trước khi bỏ mô hình hiện tại.
2. Tạo hoặc import hình học, đặt vật liệu và thông số mô phỏng.
3. Trên tab **File** của Ribbon, nhấn **Save** để lưu; nếu dự án chưa có tệp,
   hộp thoại lưu sẽ tự mở. Mũi tên cạnh **Save** chứa **Save As** và
   **Save All**. Tệp dự án dùng định dạng `.oem`.
4. Chọn **File → Open Project** để mở dự án; **Open Recent** hiển thị tối đa
   10 dự án đã lưu hoặc mở gần đây. **Close Project** đóng dự án hiện tại sau
   khi xác nhận bỏ các thay đổi chưa lưu. Sau khi mở `.oem`, giao diện mặc định
   hiển thị Geometry; kết quả đi kèm dự án vẫn được nạp để xem trong Results.
5. **Project Settings** mở bảng cấu hình mô phỏng. **Import** hiện hỗ trợ mô
   hình CAD định dạng `.step`/`.stp`; **Export** cho phép xuất mô hình STEP
   hoặc lưu dự án OEM sang vị trí khác.

## 3. Tạo và chỉnh sửa hình học 3D

### Vẽ Box hoặc Plate

1. Chọn tab **Modeling** trên Ribbon.
2. Nhấn **Draw Box** hoặc **Draw Plate**. Viewport chuyển sang Top View và
   hiện lưới tham chiếu.
3. Kéo chuột trên viewport để vẽ footprint hình chữ nhật; thả chuột để tạo
   solid. Có thể vẽ nhiều footprint liên tiếp.
4. Chọn solid trong Navigation Tree hoặc click solid trong viewport.
5. Trong Properties, chỉnh Origin X/Y/Z, Length, Width, Height và Rotation
   X/Y/Z. Plate có thêm Hole radius.
6. Nhấn **3D View** hoặc **Fit View** để đổi góc nhìn/phóng vừa mô hình.
   Kéo nền viewport để xoay góc nhìn.

Các nút **Add Plate**, **Add Box** và **Add Cylinder** tạo primitive với kích
thước ban đầu; chỉnh kích thước trong Properties. **Duplicate** tạo bản sao
dịch 5 mm. Chọn solid trong cây để xóa bằng **Remove selected**.

### Boolean

- Chọn đúng hai solid trong Navigation Tree (giữ Ctrl/Command để chọn nhiều).
- **Boolean Fuse** hợp nhất chúng.
- **Boolean Cut** lấy solid đứng trước theo thứ tự cây trừ solid đứng sau.
- Kết quả được lưu dưới dạng B-Rep; phép toán thất bại hoặc tạo hình học
  không hợp lệ sẽ báo lỗi.

### Chọn và hiển thị

Click vào bounding proxy của solid trong viewport để đồng bộ lựa chọn với
Navigation Tree. Các solid STEP/Boolean phức tạp chưa được render theo đúng
topology trong viewport; dùng STEP export hoặc solver để xử lý B-Rep thật.
Grid bật/tắt ở tab View; Fit View tự căn khung theo kích thước mô hình.

## 4. Gán vật liệu

1. Chọn solid trong Navigation Tree hoặc viewport.
2. Mở tab **Materials**.
3. Chọn **Perfect conductor (PEC)** hoặc **Dielectric**.
4. Với điện môi, nhập relative permittivity εr, conductivity σ (S/m) và
   loss tangent tanδ.

Mỗi solid hiện dùng vật liệu đồng nhất, đẳng hướng. Chưa có vật liệu phụ
thuộc tần số, tensor, từ thẩm tùy chỉnh hoặc thư viện vật liệu mở rộng.

## 5. Thiết lập port và bài toán

Trang **Excitations** có thiết lập chung cho scene CAD: frequency range, số
điểm, mesh divisions per wavelength, maximum timesteps, boundary, lumped port
và CPU/GPU. Các lựa chọn **Solver Selection** và **Domain** trên Ribbon hiện
chỉ là giao diện; chưa điều khiển backend. **Solver Options** chưa có nội dung.

### CAD lumped port

Prototype hỗ trợ **một lumped port** cho scene CAD:

1. Chọn trục **X**, **Y** hoặc **Z**.
2. Nhập tọa độ Start X/Y/Z và Stop X/Y/Z. Hai đầu mút phải chỉ khác nhau
   trên trục đã chọn, và Start phải nhỏ hơn Stop trên trục đó.
3. Nhập trở kháng port.
4. Quan sát marker đỏ trong viewport. Port phải bắc qua feed gap phù hợp,
   không xuyên vật liệu kim loại.

Đây là port đơn; chưa có nhiều CAD ports, port coaxial/discrete/Floquet hoặc
nguồn dòng/áp tùy ý. Mô hình phải có cấu trúc cấp nguồn vật lý phù hợp thì
S11 mới có ý nghĩa thiết kế. Port mặc định chỉ là giá trị khởi đầu.

## 6. Chạy, theo dõi và hủy mô phỏng

- **Run simulation:** chạy scene CAD hiện tại.
- Trong lúc chạy, xem progress indicator và log ở dock **Progress / Messages**.
- Nhấn **Cancel simulation** để dừng tiến trình solver.

Solver có giới hạn mặc định 5 triệu cells. Nếu lưới vượt giới hạn, log báo
cell count và yêu cầu giảm mesh density hoặc điều chỉnh giới hạn trong script.
Summary ghi thêm ước lượng bộ nhớ khoảng 72 bytes/cell; đây chỉ là ước tính,
không bao gồm toàn bộ overhead của hệ điều hành, backend hoặc field dumps.

## 7. Xem và xuất kết quả

Mở tab **Results**:

- CAD: S11 và waveform của scene hiện tại.
- **Open Results Folder** mở thư mục đầu ra trong file manager.

Các file OEM mẫu cũ được giữ làm dữ liệu kiểm thử; hình học và tham số được
đọc từ project, không được dựng sẵn trong ứng dụng. Ứng dụng hiện chưa có
trình hiển thị kết quả chuyên biệt cho các định dạng kết quả trong những file
mẫu đó.

Smith Chart, trường E/H thể tích 3D, transient animation, surface current,
Poynting vector và far-field cho scene CAD tùy ý chưa được triển khai. Không
diễn giải kết quả của một project khác như kết quả của mô hình CAD đang mở.

## 8. Kiểm thử

Chạy các kiểm thử đã cấu hình bằng:

```sh
ctest --test-dir build --output-on-failure
```

Các bài reference kiểm tra giới hạn rộng về S11, reciprocity, S21, cutoff,
directivity, field data và giới hạn mesh; chúng là smoke/reference checks,
không phải chứng nhận độ chính xác cho mọi cấu hình hoặc vật liệu. Việc
kiểm thử Windows/Linux hiện vẫn được hoãn.
