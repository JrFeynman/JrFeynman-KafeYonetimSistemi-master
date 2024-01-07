import javax.swing.*;
import javax.swing.event.ListSelectionEvent;
import javax.swing.event.ListSelectionListener;
import javax.swing.filechooser.FileNameExtensionFilter;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.List;
import java.io.*;

public class Kafe implements IMenu{
    private Siparis siparis = new Siparis();

    //JList'lerde kullanılmak üzere oluşturulan DefaultListModel'leri
    private DefaultListModel<String> siparisList;
    private DefaultListModel<String> sicakIceceklerList;
    private DefaultListModel<String> sogukIceceklerList;
    private DefaultListModel<String> tatlilarList;
    private DefaultListModel<String> tuzlularList;

    //dosya isimlerini tutmak için oluşturulan değişkenler
    private static final String SICAK_FILE = "KafeYonetimSistemi-master/sicakIcecek.txt";
    private static final String SOGUK_FILE = "KafeYonetimSistemi-master/sogukIcecek.txt";
    private static final String TATLILAR_FILE = "KafeYonetimSistemi-master/tatli.txt";
    private static final String TUZLULAR_FILE = "KafeYonetimSistemi-master/tuzlu.txt";

    //daha sonra DefaultListModel'lere atanacak list'ler
    private List<Urun> menu = new ArrayList<>();
    private List<Urun> sicaklar = new ArrayList<>();
    private List<Urun> soguklar = new ArrayList<>();
    private List<Urun> tatlilar = new ArrayList<>();
    private List<Urun> tuzlular = new ArrayList<>();

    //GUI birimleri
    private JPanel anaPanel, eklenenUrunler, UrunButonlari, ustPanel, urunButonlari;
    private JList list1;
    private JButton SiparisOlustur, Iptal, MenuButton, Gecmis;
    private JLabel Urunler, SicakIceceklerLabel, SogukIceceklerLabel, TatlilarLabel, TuzlularLabel, labelTF, ToplamFiyat;
    private JList SicakIcecekler;
    private JList SogukIcecekler;
    private JList Tatlilar;
    private JList Tuzlular;


    public Kafe() {
        siparisList = new DefaultListModel<>();
        sicakIceceklerList = new DefaultListModel<>();
        sogukIceceklerList = new DefaultListModel<>();
        tatlilarList = new DefaultListModel<>();
        tuzlularList = new DefaultListModel<>();
        list1.setModel(siparisList);
        SicakIcecekler.setModel(sicakIceceklerList);
        SogukIcecekler.setModel(sogukIceceklerList);
        Tatlilar.setModel(tatlilarList);
        Tuzlular.setModel(tuzlularList);

        //dosyadan menü okuma
        readMenuFromFile(SICAK_FILE,sicaklar,sicakIceceklerList);
        readMenuFromFile(SOGUK_FILE,soguklar,sogukIceceklerList);
        readMenuFromFile(TATLILAR_FILE,tatlilar,tatlilarList);
        readMenuFromFile(TUZLULAR_FILE,tuzlular,tuzlularList);

        MenuButton.addActionListener(new ActionListener() {
            @Override
            public void actionPerformed(ActionEvent e) {
                openAndModifyMenuFile();
            }
        });
        SicakIcecekler.addListSelectionListener(new ListSelectionListener() {
            @Override
            public void valueChanged(ListSelectionEvent e) {
                addSelectedItemToSiparisList(SicakIcecekler);
            }
        });
        SogukIcecekler.addListSelectionListener(new ListSelectionListener() {
            @Override
            public void valueChanged(ListSelectionEvent e) {
                addSelectedItemToSiparisList(SogukIcecekler);
            }
        });
        Tatlilar.addListSelectionListener(new ListSelectionListener() {
            @Override
            public void valueChanged(ListSelectionEvent e) {
                addSelectedItemToSiparisList(Tatlilar);
            }
        });
        Tuzlular.addListSelectionListener(new ListSelectionListener() {
            @Override
            public void valueChanged(ListSelectionEvent e) {
                addSelectedItemToSiparisList(Tuzlular);
            }
        });
        Iptal.addActionListener(new ActionListener() {
            @Override
            public void actionPerformed(ActionEvent e) {
                removeLastItem();
            }
        });
        SiparisOlustur.addActionListener(new ActionListener() {
            @Override
            public void actionPerformed(ActionEvent e) {
                if(siparis.siparisOlustur()) {
                    siparis.dosyayaYaz();

                    // sonraki siparişler için listeyi sıfırla
                    siparisList.clear();
                    // sonraki siparişler için toplam fiyatı sıfırla
                    updateToplamFiyatLabel();

                    JOptionPane.showMessageDialog(anaPanel, "Sipariş detayları başarıyla kaydedildi.");

                }
            }
        });
        Gecmis.addActionListener(new ActionListener() {
            @Override
            public void actionPerformed(ActionEvent e) {
                showSavedSiparisFiles();
            }
        });

        JFrame frame = new JFrame("gui");
        frame.setContentPane(this.anaPanel);
        frame.setSize(1200,700);
        frame.setResizable(false);
        frame.setLocationRelativeTo(null);
        frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);

        frame.setVisible(true);
    }


    public void readMenuFromFile(String path, List<Urun> list, DefaultListModel<String> listModel) {
        try (BufferedReader reader = new BufferedReader(new FileReader(path))) {
            String line;
            while ((line = reader.readLine()) != null) {
                String[] parts = line.split(",");
                if (parts.length == 3) {
                    float fiyat = Float.parseFloat(parts[0]);
                    String isim = parts[1];
                    String boy = parts[2];
                    Icecek icecek = new Icecek(fiyat, isim, boy);
                    list.add(icecek);
                    listModel.addElement(isim + " - " + boy + " - " + fiyat +"$");
                    menu.add(icecek);
                } else if (parts.length == 2) {
                    float fiyat = Float.parseFloat(parts[0]);
                    String isim = parts[1];
                    Yiyecek yiyecek = new Yiyecek(fiyat, isim);
                    list.add(yiyecek);
                    listModel.addElement(isim + " - " + fiyat +"$");
                    menu.add(yiyecek);
                }
            }
        } catch (Exception e) {
            System.out.println("Dosyadan okuma sırasında bir hata oluştu: " + e.getMessage());
        }
    }


    public void openAndModifyMenuFile() {
        JFileChooser fileChooser = new JFileChooser();
        fileChooser.setDialogTitle("Düzenlenecek text dosyasını seçiniz:");
        fileChooser.setFileFilter(new FileNameExtensionFilter("Text Files", "txt"));
        fileChooser.setCurrentDirectory(new File("."));

        int result = fileChooser.showOpenDialog(anaPanel);

        if (result == JFileChooser.APPROVE_OPTION) {
            //seçilen dosyayı getir
            File selectedFile = fileChooser.getSelectedFile();

            // seçilen dosyayı aç ve düzenle
            modifyMenuFile(selectedFile);
        }
    }

    public void modifyMenuFile(File file) {
        try (BufferedReader reader = new BufferedReader(new FileReader(file))) {
            StringBuilder content = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) {
                content.append(line).append("\n");
            }

            // dosya içeriklerini göster
            JTextArea textArea = new JTextArea(content.toString());
            textArea.setEditable(true);

            int result = JOptionPane.showConfirmDialog(
                    anaPanel,
                    new JScrollPane(textArea),
                    "Menü Düzenle",
                    JOptionPane.OK_CANCEL_OPTION,
                    JOptionPane.PLAIN_MESSAGE
            );

            if (result == JOptionPane.OK_OPTION) {
                // düzenlenen içeriği dosyaya kaydet
                try (PrintWriter writer = new PrintWriter(file)) {
                    writer.print(textArea.getText());
                }

                try{
                    String fileName = file.getName();

                    switch (fileName){
                        case "sicakIcecek.txt":
                            sicakIceceklerList.clear();
                            readMenuFromFile(SICAK_FILE, sicaklar, sicakIceceklerList);
                            SicakIcecekler.setModel(sicakIceceklerList);
                            break;
                        case "sogukIcecek.txt":
                            sogukIceceklerList.clear();
                            readMenuFromFile(SOGUK_FILE, soguklar, sogukIceceklerList);
                            SogukIcecekler.setModel(sogukIceceklerList);
                            break;
                        case "tatli.txt":
                            tatlilarList.clear();
                            readMenuFromFile(TATLILAR_FILE, tatlilar, tatlilarList);
                            Tatlilar.setModel(tatlilarList);
                            break;
                        case "tuzlu.txt":
                            tuzlularList.clear();
                            readMenuFromFile(TUZLULAR_FILE, tuzlular, tuzlularList);
                            Tuzlular.setModel(tuzlularList);
                            break;
                        default:
                            throw new UnsupportedOperationException("Geçersiz dosya: " + fileName);
                    }
                }catch (UnsupportedOperationException e) {
                    JOptionPane.showMessageDialog(
                            anaPanel,
                            e.getMessage(),
                            "Geçersiz işlem.",
                            JOptionPane.ERROR_MESSAGE
                    );
                } catch (Exception ex) {
                    System.out.println("Düzenleme sırasında hata oluştu: " + ex.getMessage());
                }

                JOptionPane.showMessageDialog(
                        anaPanel,
                        "Değişiklikler kaydedildi.",
                        "Menü Düzenleme",
                        JOptionPane.INFORMATION_MESSAGE
                );
            }
        } catch (Exception ex) {
            System.out.println("Dosya düzenlemesi sırasında hata oluştu: " + ex.getMessage());
        }
    }


    private void selectItem(String itemName) {
        for (Urun urun : menu) {
            if (urun instanceof Icecek && (urun.isim + " - " + ((Icecek) urun).boy + " - " + urun.fiyat + "$").equals(itemName)) {
                siparisList.addElement(itemName);
                siparis.urunEkle((Icecek) urun);
                updateToplamFiyatLabel();
                break;
            } else if (urun instanceof Yiyecek && (urun.isim + " - " + urun.fiyat + "$").equals(itemName)) {
                siparisList.addElement(itemName);
                siparis.urunEkle((Yiyecek) urun);
                updateToplamFiyatLabel();
                break;
            }
        }
    }

    private void updateToplamFiyatLabel() {
        float toplamFiyat = siparis.hesapla();
        ToplamFiyat.setText("Toplam Fiyat: " + toplamFiyat);
    }

    public static void main(String[] args) {
        SwingUtilities.invokeLater(new Runnable() {
            @Override
            public void run() {
                new Kafe();
            }
        });
    }

    private void removeLastItem() {
        int lastIndex = siparisList.getSize() - 1;
        if (lastIndex >= 0) {
            // son eklenen ürünü seç
            String removedItem = siparisList.getElementAt(lastIndex);

            // son eklenen ürünü listeden sil
            siparisList.removeElementAt(lastIndex);

            // ürünü siparişten sil
            for (Urun urun : menu) {
                if ((urun.isim + (urun instanceof Icecek ? " - " + ((Icecek) urun).boy : "") + " - " + urun.fiyat + "$").equals(removedItem)) {
                    siparis.urunler.remove(urun);
                    break;
                }
            }
        }

        updateToplamFiyatLabel();
    }

    private void showSavedSiparisFiles() {
        JFileChooser fileChooser = new JFileChooser();
        fileChooser.setDialogTitle("Geçmiş Sipariş Dosyalarını Seç");
        fileChooser.setFileFilter(new FileNameExtensionFilter("Text Files", "txt"));
        fileChooser.setCurrentDirectory(new File(".")); // Set the default directory

        int result = fileChooser.showOpenDialog(anaPanel);

        if (result == JFileChooser.APPROVE_OPTION) {
            File selectedFile = fileChooser.getSelectedFile();
            openAndDisplayFile(selectedFile);
        }
    }
    private void addSelectedItemToSiparisList(JList list) {
        Object selectedValue = list.getSelectedValue();
        if (selectedValue != null) {
            selectItem(selectedValue.toString());
            updateToplamFiyatLabel();
            list.clearSelection();  // ürün eklendikten sonra seçimi sil
        }
    }


    private void openAndDisplayFile(File file) {
        try (BufferedReader reader = new BufferedReader(new FileReader(file))) {
            StringBuilder content = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) {
                content.append(line).append("\n");
            }

            JTextArea textArea = new JTextArea(content.toString());
            textArea.setEditable(false);

            JOptionPane.showMessageDialog(anaPanel, new JScrollPane(textArea), "Sipariş Detayları",
                    JOptionPane.PLAIN_MESSAGE);
        } catch (Exception ex) {
            System.out.println("Error opening file: " + ex.getMessage());
        }
    }
}