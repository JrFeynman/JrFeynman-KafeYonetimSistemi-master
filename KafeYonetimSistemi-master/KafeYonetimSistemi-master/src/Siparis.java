import javax.swing.*;
import java.io.BufferedWriter;
import java.io.FileWriter;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
public class Siparis {
    List<Urun> urunler = new ArrayList<>();
    private String customerName;
    void setCustomerName(String customerName) { //müşteri ismini kaydet
        while (true) {
            try {
                if (isValidCustomerName(customerName)) {
                    this.customerName = customerName;
                    break; //isim doğru girildiyse döngüden çık ve devam et
                } else {
                    throw new InvalidCustomerNameException("Geçersiz isim: " + customerName);
                }
            } catch (InvalidCustomerNameException e) {
                JOptionPane.showMessageDialog(null, e.getMessage(), "Geçersiz isim", JOptionPane.ERROR_MESSAGE);
                customerName = promptForValidName();
            }
        }
    }
    private String promptForValidName() {
        return JOptionPane.showInputDialog(null, "Geçerli bir isim giriniz:");
    }

    private boolean isValidCustomerName(String name) {
        //isimde harf dışında ögeler varsa veya boşluklardan oluşuyorsa hata ver
        return name.matches("^[a-zA-Z ]+$") && !name.trim().isEmpty();
    }
    void urunEkle(Icecek x){
        urunler.add(x);
    }
    void urunEkle(Yiyecek x) {
        urunler.add(x);
    }

    float hesapla() {
        float total = 0;
        for (Urun urun : urunler) {
            if (urun instanceof Icecek) {
                total += urun.fiyat;
            } else if (urun instanceof Yiyecek) {
                total += urun.fiyat;
            }
        }
        return total;
    }
    String stringYaz(Icecek x){
        return x.isim+" - "+x.boy+" - "+x.fiyat;
    }
    String stringYaz(Yiyecek x){
        return x.isim+" - "+x.fiyat;
    }
    boolean siparisOlustur() {
        try {
            if (urunler.isEmpty()) {
                throw new EmptyOrderException("Sipariş boş. Kaydetmeden önce ürün ekleyiniz.");
            }

            // Müşteri ismi girişi
            String customerName = JOptionPane.showInputDialog(null, "Müşteri ismini giriniz");

            // Müşteri ismini kaydet
            setCustomerName(customerName);

            return true; //sipariş oluşturma başarılı

        } catch (EmptyOrderException e) {
            JOptionPane.showMessageDialog(null, e.getMessage(), "Boş sipariş.", JOptionPane.ERROR_MESSAGE);
            return false;
        } catch (Exception e) {
            System.out.println("Sipariş oluşturma sırasında bir hata meydana geldi: " + e.getMessage());
            return false;
        }
    }
    void dosyayaYaz() {
        //siparişi txt dosyasına kaydet
        try {
            //anlık saat ve tarih, dosya isminde kullanılacak
            String timeStamp = new SimpleDateFormat("yyyyMMdd_HHmmss").format(new Date());
            String dosyaAdi = "siparis_" + customerName + "_" + timeStamp + ".txt";

            try (BufferedWriter yazici = new BufferedWriter(new FileWriter(dosyaAdi))) {
                yazici.write("Müşteri Adı: " + customerName);
                yazici.newLine();

                for (Urun urun : urunler) {
                    if (urun instanceof Icecek) {
                        yazici.write(stringYaz((Icecek) urun));
                    } else if (urun instanceof Yiyecek) {
                        yazici.write(stringYaz((Yiyecek) urun));
                    }
                    yazici.newLine();
                }
                yazici.write("Toplam Fiyat: " + hesapla());
            }

            System.out.println("Sipariş detayları başarıyla kaydedildi. Dosya adı: " + dosyaAdi);

            urunler.clear();
        }
        catch (Exception e) {
            System.out.println("Dosyaya yazma sırasında hata oluştu: " + e.getMessage());
        }
    }

}