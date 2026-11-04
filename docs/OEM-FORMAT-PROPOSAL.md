# Định dạng dự án `.oem`: quyết định và triển khai

Tài liệu ghi lại các quyết định định dạng và hướng triển khai. Mục tiêu là tái
sử dụng định dạng sẵn có cho dữ liệu mà các định dạng đó đã giải quyết tốt,
chỉ bổ sung metadata cho parameters, feature history và liên kết assets.

**Đã chốt:** `.oem` là archive ZIP có XML manifest; không hỗ trợ migration; đây
là version đầu tiên của định dạng dự án.

## Tóm tắt

Không dùng một schema tự tạo để mô tả lại toàn bộ geometry, vật liệu, solver và
kết quả. Thay vào đó:

1. Dùng **XML native của openEMS/CSXCAD** làm mô hình solver-ready cho geometry,
   vật liệu và cấu hình mô phỏng.
2. Dùng **STEP AP242** làm geometry B-rep trao đổi hoặc dự phòng cho hình nhập
   và hình chưa có primitive tương ứng.
3. Dùng **Touchstone** cho kết quả S-parameter; dùng **HDF5** hoặc VTK cho dữ
   liệu trường. Có thể dùng **XDMF** để mô tả mesh và liên kết tới HDF5.
4. Dùng `project.xml` trong archive `.oem` cho parameters/expressions, liên kết
   parameter tới hình học/vật liệu, cấu hình tối ưu, snapshot đầu vào mỗi lần
   chạy và tham chiếu tới các payload theo định dạng chuẩn.

## Các định dạng được khảo sát

| Dữ liệu | Định dạng đề xuất | Mức độ phù hợp và giới hạn |
|---|---|---|
| Geometry, vật liệu, excitation và thiết lập solver | openEMS/CSXCAD XML | Phù hợp nhất với solver đang dùng. Tài liệu openEMS mô tả cả lưu geometry riêng và lưu geometry cùng simulation qua `Write2XML()`. CSXCAD gắn primitive vào property, trong đó có material properties. Đây nên là mô hình solver-ready, sau khi biểu thức đã được tính thành số. |
| Hình CAD chính xác và geometry nhập | STEP AP242 | Định dạng trao đổi 3D trung lập, thích hợp làm payload B-rep. Không nên coi STEP là nơi lưu nguồn công thức hoặc lịch sử feature của ứng dụng. |
| S-parameters | Touchstone 2.1 | Chuẩn chuyên dụng để trao đổi dữ liệu mạng nhiều cổng. Các đại lượng như VSWR có thể được tính lại từ S-parameters nếu cần. |
| Trường điện từ và dữ liệu số lớn | openEMS HDF5 hoặc VTK; XDMF tùy nhu cầu hiển thị | HDF5/VTK thích hợp cho dữ liệu trường lớn. XDMF dùng XML mô tả cấu trúc dữ liệu và có thể tham chiếu payload HDF5. |
| Geometry parametric và feature history | OCCT OCAF hoặc feature graph của ứng dụng | OCAF có thể giúp tổ chức document/attributes và topology history trong ứng dụng OCCT. Đây là framework dữ liệu ứng dụng, không phải một chuẩn trao đổi chung thay thế openEMS XML hoặc STEP. |

Tài liệu [openEMS Model I/O](https://docs.openems.de/en/latest/concepts/model_io.html)
mô tả `Write2XML()` cho mô hình geometry và simulation. Danh sách primitive và
material properties nằm trong tài liệu
[CSXCAD](https://wiki.openems.de/index.php/CSXCAD.html). Chuẩn
[STEP AP242](https://webstore.ansi.org/standards/iso/ISO103032422025),
[Touchstone 2.1](https://ibis.org/~ibisorg/touchstone_ver2.1/touchstone_ver2_1.pdf),
[openEMS HDF5 dumps](https://docs.openems.de/en/stable/reference/dump_hdf5.html)
và [XDMF](https://www.xdmf.org/) là các tham khảo cho geometry/results.

Tài liệu chính thức của [HFSS về Variables](https://ansyshelp.ansys.com/public/Views/Secured/Electronics/v251/en/Subsystems/HFSS/Content/Variables/WorkingwithVariables.htm)
mô tả biến/biểu thức có thể điều khiển kích thước và thuộc tính vật liệu; tài
liệu [CST về parameterization và optimization](https://www.3ds.com/products/simulia/cst-studio-suite/automatic-optimization)
cũng mô tả parameter hóa kích thước hình học và vật liệu. Đây là cơ sở để
parameter là dữ liệu thiết kế riêng, không chỉ là giá trị cuối cùng của một
primitive.

## Cấu trúc manifest `project.xml`

`.oem` là ZIP; bên trong `project.xml` lưu version và liên kết geometry,
parameters, materials, feature history, solver assets và kết quả. Ví dụ rút gọn:

```xml
<oem-project format-version="0.0.1">
  <geometry>
    <solid id="body-patch"
           name="Patch"
           kind="box"
           material-ref="mat-pec"
           x-mm="0"
           y-mm="0"
           z-mm="1.524"
           length-mm="12"
           width-mm="40"
           height-mm="0.1"/>
  </geometry>

  <design>
    <parameters>
      <parameter id="p-feed-offset"
                 name="feed_offset"
                 unit="mm"
                 expression="6"
                 evaluated-value="6"
                 role="design-variable"
                 optimizable="true"
                 minimum="4"
                 maximum="8"
                 step="0.1"/>
      <parameter id="p-patch-width"
                 name="patch_width"
                 unit="mm"
                 expression="2 * feed_offset"
                 evaluated-value="12"
                 role="derived"
                 optimizable="false"/>
    </parameters>

    <materials>
      <material id="mat-pec" type="pec"/>
      <material id="mat-substrate"
                type="dielectric"
                relative-permittivity="3.38"
                conductivity-s-per-m="0"
                loss-tangent="0"/>
    </materials>

    <feature-history>
      <feature id="feature-patch"
               type="primitive.box"
               output-id="body-patch"
               output-kind="solid"
               material-ref="mat-pec">
        <argument name="length"
                  expression="2 * feed_offset"
                  evaluated-value="12"
                  unit="mm"
                  parameter-ref="p-patch-width"/>
      </feature>
    </feature-history>
  </design>

  <!-- Full frequency, port, boundary, mesh and model settings are stored here. -->

  <solver-assets format="openEMS-CSXCAD-XML">
    <file path="models/openems.xml"/>
  </solver-assets>

  <results kind="antenna">
    <file path="results/s11_antenna.s1p"/>
    <file path="results/s11_antenna.csv"/>
  </results>
</oem-project>
```

Các thuộc tính trong ví dụ được rút gọn để dễ đọc; project thật còn lưu toàn bộ
tham số mô phỏng và feature arguments.

### Parameters và tối ưu

- Mỗi parameter có ID ổn định, tên hiển thị, expression, unit và giá trị đã
  evaluate.
- Geometry/material tham chiếu parameter bằng ID; không chép riêng giá trị số
  vào từng nơi rồi mất quan hệ công thức.
- Design variables bổ sung giới hạn, step/scale và trạng thái có tham gia tối
  ưu hay không. Parameter dẫn xuất không nhất thiết là design variable.
- Cấu hình tối ưu lưu riêng objective, constraints, thuật toán và tiêu chí dừng.
- Mỗi lần chạy lưu snapshot các giá trị parameter thực tế, solver settings và
  đường dẫn/payload kết quả để kết quả luôn truy ngược được về thiết kế.
- Expression engine cần grammar hữu hạn, kiểm tra đơn vị, phát hiện phụ thuộc
  vòng và báo lỗi rõ ràng; không đánh giá expression bằng `eval` tổng quát.

### Vật liệu

Mỗi body có `material-ref`. Các body cùng vật liệu có thể cùng tham chiếu một
material ID; body khác vật liệu phải tham chiếu material ID khác. Native
CSXCAD model được tạo với property/material tương ứng và primitive thuộc về
property đó.

### Hình học và lịch sử

**Yêu cầu đã chốt:** lưu đầy đủ feature history. Cây feature là nguồn thiết kế
chính; mô hình openEMS XML và mesh dùng cho solver là dữ liệu được sinh từ cây
đó, không thay thế lịch sử thiết kế.

History phải giữ ID của feature, thứ tự/phụ thuộc, input profile/path, parameter
refs, vật liệu và kết quả hình học. Tối thiểu các thao tác tạo hình hiện có
(primitive, transform, Boolean) và các thao tác sweep đều phải có node riêng.
Mở file phải cho phép sửa parameter đầu vào rồi tính lại các feature phụ thuộc.

#### Curve và sweep

Curve cần là geometry có thể tái sử dụng trong history, không chỉ là tập điểm
vẽ tạm. Tùy loại đường cong, cần lưu loại curve và dữ liệu định nghĩa như điểm
đầu/cuối, tâm/bán kính, control points, knots và weights. Curve được tham chiếu
bởi profile, path hoặc feature khác.

Sweep cần lưu riêng profile và spine/path:

- **Sweep curve thành surface:** profile là curve/wire; output là surface hoặc
  shell. Đây là geometry 2D, không được ngầm coi thành volume.
- **Sweep face thành solid:** profile là face có biên; output phải được kiểm tra
  là solid hợp lệ/đóng trước khi đưa vào solver.
- Cả hai lưu thêm các thiết lập ảnh hưởng hình dạng như orientation/trihedron,
  scale/twist nếu operation hỗ trợ, và các parameter được tham chiếu.

#### Chuyển mặt kín thành khối đặc

Phải có feature riêng để chuyển một closed shell (tập các mặt tạo thành vỏ
kín) thành solid. Đây là thao tác tạo volume từ vỏ mặt đã kín, **không phải**
đùn/thêm thickness. Feature giữ tham chiếu tới shell đầu vào để khi upstream
curve/sweep thay đổi, solid có thể được tạo lại.

Trước khi tạo solid, kiểm tra shell khép kín, topology hợp lệ và bao quanh thể
tích hữu hạn, dương. Sau khi tạo, tiếp tục kiểm tra solid bằng bộ kiểm tra
topology OCCT và kiểm tra thể tích. Nếu mặt còn hở, tự giao hoặc không tạo được
volume, phải báo lỗi; không tự vá hay thêm thickness ngầm. Đóng/cap mặt hở nếu
cần phải là feature riêng có tham số.

Ví dụ history:

```text
circle(profile, radius=r)
→ line(path, length=L)
→ sweep-surface(profile, path)
→ make-solid(closed-shell)
→ assign-material(body, mat-pec)
```

#### Chuyển hình học sang openEMS

Không nên ghi sweep/curve thành một tag XML tự tạo rồi giả định solver hiểu.
Khi chạy, adapter phải chuyển feature tree đã evaluate sang primitive native
CSXCAD nếu có thể; nếu không, dùng polyhedron tam giác.

openEMS có primitive curve/polygon và polyhedron. Tài liệu polyhedron mô tả
polyhedron là hình 3D được định nghĩa bằng bề mặt đóng, vertices và faces; bộ
đọc polyhedron nhận STL/PLY. Vì vậy:

- **Solid sweep:** có thể tessellate B-rep thành STL/PLY kín, manifold, normals
  nhất quán, rồi đưa vào CSXCAD polyhedron. Phải kiểm tra độ phân giải mesh so
  với bước lưới solver và giữ material property của body.
- **Mặt kín → solid:** chỉ solid đã qua kiểm tra volume mới được tessellate thành
  polyhedron cho solver; không gửi shell mở như thể nó là solid.
- **Surface sweep:** bề mặt mở không phải polyhedron kín. Nếu dùng như PEC sheet,
  cần có đường chuyển đổi 2D/sheet được kiểm chứng với openEMS. Nếu vật liệu
  cần thể tích, phải có thickness rõ ràng để tạo solid; không tự thêm thickness
  ngầm. Trường hợp không có representation solver hợp lệ phải bị từ chối trước
  khi chạy, với thông báo cụ thể.

Mỗi feature sweep/solidify cần có kiểm thử chuyển đổi: profile/path mở lại giống
ban đầu, shell kín tạo được solid có volume, shell hở bị từ chối, solver mesh
không hở/non-manifold, material được gán đúng, và smoke simulation chạy được
cho các surface/solid cases được hỗ trợ.

Với hình không biểu diễn được bằng primitive/feature hiện có, lưu STEP AP242
làm payload B-rep. Không nên chuyển mọi geometry thành lưới tam giác nếu cần
giữ tính chính xác CAD hoặc chỉnh sửa tham số về sau.

## Đóng gói đã chọn

**Đã chọn:** `.oem` là archive ZIP tự chứa. XML manifest vẫn là XML; các
payload lớn giữ nguyên định dạng chuẩn, không mã hóa base64.

```text
project.oem
├── project.xml
├── models/openems.xml
├── geometry/*.step
└── results/
    ├── network/*.sNp
    └── fields/*.h5
```

`project.xml` khai báo version và đường dẫn tương đối bên trong archive. Các
payload được mở bằng reader tương ứng; geometry STEP, model openEMS và kết quả
HDF5/Touchstone không cần bị chuyển sang một biểu diễn tự tạo.

## Version và tương thích

Định dạng dự án được phát hành lần đầu với `format-version="0.0.1"`. Không cần
migration từ prototype hiện tại; các file thử nghiệm theo schema cũ không được
coi là định dạng phát hành cần tương thích.

Reader phải từ chối version không hỗ trợ, kiểm tra tham chiếu parameter/material
bị thiếu, expression không hợp lệ, đơn vị sai và file asset không an toàn.

## Raw field dumps là gì?

Raw field dump là các mẫu trường điện từ **chưa qua xử lý tổng hợp**, được ghi
trên vùng mesh theo không gian và có thể theo thời gian/tần số. Ví dụ trong
workflow antenna hiện tại là `Ef_patch.h5` và `Hf_patch.h5`; các file
`nf2ff_*.h5` là dữ liệu bề mặt phục vụ phép biến đổi near-field-to-far-field.

Các file như `s11_antenna.csv`, `far_field_antenna.csv` và
`near_field_antenna.csv` là kết quả đã xử lý/được trích xuất để hiển thị. Chúng
nhỏ hơn, đủ để dựng lại các biểu đồ hiện có, nhưng không nhất thiết đủ để tính
lại một lát cắt trường khác hoặc chạy post-processing mới. Raw dumps hữu ích
khi cần hậu xử lý lại, nhưng có thể làm archive `.oem` lớn hơn nhiều.

**Đã chốt cho version đầu:** không đóng gói raw field dumps vào `.oem`; luôn lưu
các kết quả đã xử lý. Nếu cần lát cắt trường khác hoặc post-processing mới từ
dữ liệu gốc, cần chạy lại mô phỏng.

## Giải thích feature history

“Feature history” là lưu **chuỗi thao tác đã tạo ra khối**, chứ không chỉ lưu
khối cuối cùng. Ví dụ:

```text
Tạo box (length=L, width=W, height=H)
→ tạo cylinder (radius=R)
→ Boolean Cut cylinder khỏi box
→ Fillet cạnh (radius=F)
```

Mục tiêu của history là cho phép sửa `L`, `R` hoặc `F` rồi tính lại các thao tác
phía sau. Cách này gần với cách thiết kế theo lịch sử thao tác, nhưng cần lưu
quan hệ phụ thuộc giữa feature và xử lý những trường hợp tham chiếu cạnh/mặt
thay đổi sau khi geometry được tính lại. Trong bản triển khai đầu, history và
shape cache được lưu; bộ chỉnh sửa tổng quát để replay mọi feature sau khi sửa
tham số vẫn là phần cần hoàn thiện.

“Primitives + parameters” chỉ lưu các khối cơ bản và tham số của từng khối, ví
dụ `Box(length=L, width=W, height=H)` hoặc `Sphere(radius=R)`. Các khối này vẫn
chỉnh sửa được theo tham số; tuy nhiên, nếu không lưu Boolean/fillet như các
feature riêng, file chỉ giữ kết quả cuối của thao tác đó. Với geometry nhập
hoặc chưa hỗ trợ, STEP giữ hình dạng B-rep chính xác nhưng không giữ lịch sử
thao tác nguồn.

**Quyết định đã chốt:** sau khi mở dự án, có thể sửa từng bước như Boolean Cut,
Fillet hoặc Sweep và tính lại các feature phụ thuộc.

- [x] Có — cần lưu feature history đầy đủ.

## Nguồn tham khảo

- [openEMS: Saving, Loading & Interoperability](https://docs.openems.de/en/latest/concepts/model_io.html)
- [CSXCAD: properties, materials and primitives](https://wiki.openems.de/index.php/CSXCAD.html)
- [ISO 10303-242: Managed model-based 3D engineering](https://webstore.ansi.org/standards/iso/ISO103032422025)
- [IBIS Open Forum: Touchstone 2.1](https://ibis.org/~ibisorg/touchstone_ver2.1/touchstone_ver2_1.pdf)
- [openEMS: Field Dump HDF5 format](https://docs.openems.de/en/stable/reference/dump_hdf5.html)
- [openEMS: Primitives and polyhedra](https://docs.openems.de/en/latest/concepts/primitives.html)
- [openEMS: CSPrimPolyhedron](https://docs.openems.de/en/latest/python/CSXCAD/CSPrimitives/CSPrimPolyhedron.html)
- [Open CASCADE: BRepOffsetAPI_MakePipe sweep](https://occt3d.com/dev/doc/refman/html/class_b_rep_offset_a_p_i___make_pipe.html)
- [Open CASCADE: BRepBuilderAPI_MakeSolid from shells](https://dev.opencascade.org/doc/occt-7.3.0/refman/html/class_b_rep_builder_a_p_i___make_solid.html)
- [XDMF: XML metadata with HDF5 heavy data](https://www.xdmf.org/)
- [Ansys HFSS: Working with Variables](https://ansyshelp.ansys.com/public/Views/Secured/Electronics/v251/en/Subsystems/HFSS/Content/Variables/WorkingwithVariables.htm)
- [Dassault Systèmes: CST parameterization and optimization](https://www.3ds.com/products/simulia/cst-studio-suite/automatic-optimization)
- [Open CASCADE: OCAF](https://occt3d.com/dev/doc/overview/html/occt_user_guides__ocaf.html)
